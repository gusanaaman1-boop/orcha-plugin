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

    a.onsetSample = onset;

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

    // Slot index breaks ties: two samples can share a centroid exactly - the
    // KIT feature loads ONE file into every slot - and std::sort would then
    // hand out roles differently on different standard libraries.
    std::sort (loaded.begin(), loaded.end(), [] (const Slot& x, const Slot& y)
    {
        const float cx = x.s->analysis.spectralCentroidHz;
        const float cy = y.s->analysis.spectralCentroidHz;
        if (cx != cy)
            return cx < cy;
        return x.index < y.index;
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

std::vector<int> SampleAnalyzer::detectOnsets (const juce::AudioBuffer<float>& buffer,
                                               double sampleRate)
{
    std::vector<int> onsets;
    const int n = buffer.getNumSamples();
    if (n == 0 || sampleRate <= 0.0)
        return onsets;

    // Smoothed rectified envelope; an onset is a rise well above the local
    // floor, at least 90 ms after the previous one.
    const float attack = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.002));
    const float release = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.05));
    float env = 0.0f, floorEnv = 0.0f;
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, n));
    if (peak < 1.0e-4f)
        return onsets;
    const int minGap = (int) (sampleRate * 0.09);
    int last = -minGap;
    for (int i = 0; i < n; ++i)
    {
        float x = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            x = juce::jmax (x, std::abs (buffer.getSample (ch, i)));
        env += (x - env) * (x > env ? attack : release);
        floorEnv += (env - floorEnv) * 0.0005f;
        if (env > peak * 0.18f && env > floorEnv * 2.2f && i - last >= minGap)
        {
            onsets.push_back (juce::jmax (0, i - (int) (sampleRate * 0.003)));
            last = i;
        }
    }
    return onsets;
}

std::vector<SampleAnalyzer::KitSlice> SampleAnalyzer::chooseKitSlices (
    const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    std::vector<KitSlice> out;
    const auto onsets = detectOnsets (buffer, sampleRate);
    if (onsets.size() < 2)
        return out;   // low confidence: the caller falls back, no pretending

    // Slice = onset to next onset (capped at 0.8 s), analyzed individually.
    struct Cand { KitSlice slice; SampleAnalysis a; };
    std::vector<Cand> cands;
    const int n = buffer.getNumSamples();
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int from = onsets[i];
        const int to = juce::jmin (n,
            juce::jmin (i + 1 < onsets.size() ? onsets[i + 1] : n,
                        from + (int) (sampleRate * 0.8)));
        if (to - from < (int) (sampleRate * 0.03))
            continue;
        juce::AudioBuffer<float> sub (buffer.getNumChannels(), to - from);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            sub.copyFrom (ch, 0, buffer, ch, from, to - from);
        Cand c;
        c.slice = { (float) from / (float) n, (float) to / (float) n };
        c.a = analyze (sub, sampleRate);
        if (c.a.transientStrength > 0.15f)
            cands.push_back (std::move (c));
    }
    if (cands.size() < 2)
        return out;

    // Start position breaks ties, for the same reason: two slices of one loop
    // can measure the same brightness, and which one becomes LOW must not
    // depend on the standard library.
    std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b)
    {
        if (a.a.spectralCentroidHz != b.a.spectralCentroidHz)
            return a.a.spectralCentroidHz < b.a.spectralCentroidHz;
        return a.slice.start < b.slice.start;
    });
    // Darkest -> LOW, brightest -> HIGH, most-middling -> MID.
    out.push_back (cands.front().slice);
    if (cands.size() >= 3)
        out.push_back (cands[cands.size() / 2].slice);
    out.push_back (cands.back().slice);
    return out;
}

} // namespace orcha
