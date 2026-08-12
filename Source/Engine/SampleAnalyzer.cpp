#include "SampleAnalyzer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

namespace orcha
{

SampleAnalysis SampleAnalyzer::analyze (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    SampleAnalysis a;
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || sampleRate <= 0.0)
        return a;

    a.durationSeconds = numSamples / sampleRate;

    // Mono mixdown view for the measurements below.
    std::vector<float> mono ((size_t) numSamples, 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[(size_t) i] += src[i];
    }
    const float chScale = 1.0f / (float) juce::jmax (1, buffer.getNumChannels());
    for (auto& v : mono) v *= chScale;

    // Onset: the first sample that crosses 25% of the file's peak. Stems and
    // loops often lead with silence, and every measurement below must anchor
    // on the sound, not on the file start.
    float filePeak = 0.0f;
    for (auto v : mono) filePeak = juce::jmax (filePeak, std::abs (v));
    int onset = 0;
    for (int i = 0; i < numSamples; ++i)
        if (std::abs (mono[(size_t) i]) > filePeak * 0.25f)
        {
            onset = juce::jmax (0, i - (int) (sampleRate * 0.002));
            break;
        }

    // Transient strength: peak of the first 20 ms after the onset against the
    // overall RMS. A percussive one-shot fronts nearly all of its energy.
    const int attackLen = juce::jmin (numSamples - onset, (int) (sampleRate * 0.02));
    float attackPeak = 0.0f;
    for (int i = 0; i < attackLen; ++i)
        attackPeak = juce::jmax (attackPeak, std::abs (mono[(size_t) (onset + i)]));

    double sumSq = 0.0;
    for (auto v : mono) sumSq += (double) v * v;
    const float rms = (float) std::sqrt (sumSq / numSamples);
    a.transientStrength = rms > 1.0e-6f
        ? juce::jlimit (0.0f, 1.0f, (attackPeak / (rms * 4.0f)))
        : 0.0f;

    // Spectral centroid + low-band ratio from an FFT over a window starting at
    // the onset (the body of the sound, not leading silence or the tail).
    constexpr int fftOrder = 12;                 // 4096 points
    constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
    const int windowLen = juce::jmin (numSamples - onset, fftSize);
    for (int i = 0; i < windowLen; ++i)
    {
        // Hann window keeps the tail truncation from smearing the spectrum.
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                * (float) i / (float) fftSize);
        fftData[(size_t) i] = mono[(size_t) (onset + i)] * w;
    }
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    double num = 0.0, den = 0.0, lowE = 0.0;
    const double binHz = sampleRate / fftSize;
    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const double mag = fftData[(size_t) bin];
        const double e = mag * mag;
        const double hz = bin * binHz;
        num += e * hz;
        den += e;
        if (hz < 200.0)
            lowE += e;
    }
    a.spectralCentroidHz = den > 0.0 ? (float) (num / den) : 0.0f;
    a.lowEnergyRatio    = den > 0.0 ? (float) (lowE / den) : 0.0f;

    // One-shot: short, and the last quarter is much quieter than the attack.
    double tailSq = 0.0;
    const int tailStart = numSamples * 3 / 4;
    for (int i = tailStart; i < numSamples; ++i)
        tailSq += (double) mono[(size_t) i] * mono[(size_t) i];
    const float tailRms = (float) std::sqrt (tailSq / juce::jmax (1, numSamples - tailStart));
    a.isOneShot = a.durationSeconds < 1.5 && tailRms < attackPeak * 0.3f;

    return a;
}

RoleMap SampleAnalyzer::assignRoles (std::vector<InputSample::Ptr>& samples)
{
    // Collect loaded slots, ordered by spectral centroid (dark -> bright).
    struct Slot { int index; const InputSample* s; };
    std::vector<Slot> loaded;
    for (int i = 0; i < (int) samples.size(); ++i)
        if (samples[(size_t) i] != nullptr)
            loaded.push_back ({ i, samples[(size_t) i].get() });

    RoleMap map;
    if (loaded.empty())
        return map;

    std::sort (loaded.begin(), loaded.end(), [] (const Slot& x, const Slot& y)
    {
        return x.s->analysis.spectralCentroidHz < y.s->analysis.spectralCentroidHz;
    });

    // Manual roles win first.
    std::vector<Slot> autos;
    for (const auto& sl : loaded)
    {
        switch (sl.s->userRole)
        {
            case Role::LOW:  map.low = sl.index;  break;
            case Role::MID:  map.mid = sl.index;  break;
            case Role::HIGH: map.high = sl.index; break;
            case Role::FX:   map.fx = sl.index;   break;
            case Role::AUTO: autos.push_back (sl); break;
        }
    }

    // Fill remaining roles dark->LOW, bright->HIGH, middle->MID. A long,
    // soft-attack file prefers FX when everything else is covered.
    auto place = [&map] (int index, Role r)
    {
        switch (r)
        {
            case Role::LOW:  if (map.low  < 0) { map.low = index;  return true; } break;
            case Role::MID:  if (map.mid  < 0) { map.mid = index;  return true; } break;
            case Role::HIGH: if (map.high < 0) { map.high = index; return true; } break;
            case Role::FX:   if (map.fx   < 0) { map.fx = index;   return true; } break;
            case Role::AUTO: break;
        }
        return false;
    };

    if (autos.size() == 1)
    {
        const auto& only = autos.front();
        if (! place (only.index, Role::LOW))
            if (! place (only.index, Role::MID))
                place (only.index, Role::HIGH);
    }
    else if (! autos.empty())
    {
        // Darkest to LOW, brightest to HIGH, the rest to MID/FX in order.
        place (autos.front().index, Role::LOW);
        place (autos.back().index, Role::HIGH);
        for (size_t i = 1; i + 1 < autos.size(); ++i)
        {
            const auto& mid = autos[i];
            const bool fxLike = ! mid.s->analysis.isOneShot
                                && mid.s->analysis.transientStrength < 0.3f;
            if (! (fxLike && place (mid.index, Role::FX)))
                if (! place (mid.index, Role::MID))
                    place (mid.index, Role::FX);
        }
    }

    // Every role a generator can ask for must resolve to some loaded slot.
    const int fallback = map.low >= 0 ? map.low
                       : map.mid >= 0 ? map.mid
                       : map.high >= 0 ? map.high : map.fx;
    if (map.low  < 0) map.low  = fallback;
    if (map.mid  < 0) map.mid  = map.low;
    if (map.high < 0) map.high = map.mid;
    if (map.fx   < 0) map.fx   = map.high;

    // Publish the resolved role back onto the samples (for the card UI).
    auto resolved = [&map] (int index)
    {
        if (map.low == index)  return Role::LOW;
        if (map.mid == index)  return Role::MID;
        if (map.high == index) return Role::HIGH;
        return Role::FX;
    };
    for (auto& sl : loaded)
    {
        auto& slotPtr = samples[(size_t) sl.index];
        if (slotPtr->resolvedRole != resolved (sl.index))
        {
            auto copy = std::make_shared<InputSample> (*slotPtr);
            copy->resolvedRole = resolved (sl.index);
            slotPtr = std::move (copy);
        }
    }
    return map;
}

} // namespace orcha
