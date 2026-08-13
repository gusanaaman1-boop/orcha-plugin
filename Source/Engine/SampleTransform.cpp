#include "SampleTransform.h"
#include "SampleAnalyzer.h"

namespace orcha
{

InputSample::Ptr SampleTransform::apply (const InputSample& raw, Settings settings)
{
    auto out = std::make_shared<InputSample> (raw);
    auto& buf = out->buffer;
    const int chans = buf.getNumChannels();
    int numSamples = buf.getNumSamples();
    if (numSamples == 0)
        return out;

    if (settings.trimTail)
    {
        // -48 dBFS floor; keep a 5 ms pad so attacks are never clipped off.
        const float floorLevel = juce::Decibels::decibelsToGain (-48.0f);
        const int pad = (int) (raw.sourceSampleRate * 0.005);
        int first = numSamples, last = -1;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (std::abs (d[i]) > floorLevel)
                {
                    first = juce::jmin (first, i);
                    break;
                }
            for (int i = numSamples - 1; i >= 0; --i)
                if (std::abs (d[i]) > floorLevel)
                {
                    last = juce::jmax (last, i);
                    break;
                }
        }
        if (last > first)
        {
            const int start = juce::jmax (0, first - pad);
            const int end = juce::jmin (numSamples, last + 1 + pad);
            juce::AudioBuffer<float> cropped (chans, end - start);
            for (int ch = 0; ch < chans; ++ch)
                cropped.copyFrom (ch, 0, buf, ch, start, end - start);
            // Short edge fades so the cut itself can never click.
            const int fade = juce::jmin (cropped.getNumSamples() / 4,
                                         (int) (raw.sourceSampleRate * 0.003));
            if (fade > 0)
            {
                cropped.applyGainRamp (0, fade, 0.0f, 1.0f);
                cropped.applyGainRamp (cropped.getNumSamples() - fade, fade, 1.0f, 0.0f);
            }
            buf = std::move (cropped);
            numSamples = buf.getNumSamples();
        }
    }

    // Crop to the kept region - both edges are the user's to drag.
    const float s0 = juce::jlimit (0.0f, 0.95f, settings.start);
    const float s1 = juce::jlimit (s0 + 0.02f, 1.0f, settings.end);
    if (settings.isCropped())
    {
        const int from = (int) ((float) numSamples * s0);
        const int newLen = juce::jmax (16, (int) ((float) numSamples * (s1 - s0)));
        juce::AudioBuffer<float> cropped (chans, juce::jmin (newLen, numSamples - from));
        for (int ch = 0; ch < chans; ++ch)
            cropped.copyFrom (ch, 0, buf, ch, from, cropped.getNumSamples());
        buf = std::move (cropped);
        numSamples = buf.getNumSamples();
    }

    // Light user fades from each edge, on top of the always-on 3 ms
    // anti-click ramps.
    const int fadeInLen = juce::jmax ((int) (raw.sourceSampleRate * 0.003),
        (int) ((float) numSamples * juce::jlimit (0.0f, 0.5f, settings.fadeIn)));
    const int fadeOutLen = juce::jmax ((int) (raw.sourceSampleRate * 0.003),
        (int) ((float) numSamples * juce::jlimit (0.0f, 0.5f, settings.fadeOut)));
    if (settings.isCropped() || settings.fadeIn > 0.001f)
        buf.applyGainRamp (0, juce::jmin (fadeInLen, numSamples), 0.0f, 1.0f);
    if (settings.isCropped() || settings.fadeOut > 0.001f)
        buf.applyGainRamp (juce::jmax (0, numSamples - fadeOutLen),
                           juce::jmin (fadeOutLen, numSamples), 1.0f, 0.0f);

    if (settings.reverse)
        buf.reverse (0, numSamples);

    out->analysis = SampleAnalyzer::analyze (buf, out->sourceSampleRate);
    return out;
}

} // namespace orcha
