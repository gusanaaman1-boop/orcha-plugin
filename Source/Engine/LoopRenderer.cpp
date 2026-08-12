#include "LoopRenderer.h"
#include <cmath>

namespace orcha
{

namespace
{
    // Linear-interp resampling read of one source channel.
    inline float readInterp (const float* src, int len, double pos)
    {
        const int i = (int) pos;
        if (i + 1 >= len)
            return i < len ? src[i] : 0.0f;
        const float frac = (float) (pos - i);
        return src[i] + (src[i + 1] - src[i]) * frac;
    }
}

juce::AudioBuffer<float> LoopRenderer::render (const Pattern& pattern, const Context& ctx)
{
    const double stepSec = (60.0 / ctx.bpm) / 4.0;             // one 16th
    const int loopLen = juce::roundToInt (pattern.settings.bars * 4.0
                                          * (60.0 / ctx.bpm) * ctx.sampleRate);
    juce::AudioBuffer<float> out (2, juce::jmax (1, loopLen));
    out.clear();

    const int fadeLen = juce::jmax (8, (int) (ctx.sampleRate * 0.003)); // 3 ms

    for (const auto& e : pattern.events)
    {
        const int slot = ctx.roleMap.slotFor (e.role);
        if (slot < 0 || slot >= (int) ctx.samples.size())
            continue;
        const auto& sample = ctx.samples[(size_t) slot];
        if (sample == nullptr || sample->buffer.getNumSamples() == 0)
            continue;

        // Swing delays odd 16th steps by up to half a step.
        double pos = e.pos;
        const bool oddStep = (juce::roundToInt (std::floor (pos)) % 2) == 1
                             && std::abs (pos - std::floor (pos)) < 0.01;
        double timeSec = pos * stepSec
                       + (oddStep ? pattern.swing * stepSec * 0.5 : 0.0)
                       + e.microMs * 0.001;
        if (timeSec < 0.0)
            timeSec = 0.0;
        const int startSample = (int) (timeSec * ctx.sampleRate);
        if (startSample >= loopLen)
            continue;

        // One loaded sample covering several roles still needs the roles to
        // sound different: non-LOW roles borrow pitch and a shorter gate.
        int pitch = e.pitchSemis;
        float gainScale = 1.0f;
        double gate = e.gateSteps;
        // Loop-like source files (stems, phrases) retrigger as slices, not as
        // full playthroughs - otherwise every event smears into the next.
        if (! sample->analysis.isOneShot && gate <= 0.0)
            gate = e.role == Role::LOW ? 4.0 : 2.0;
        if (slot == ctx.roleMap.low && e.role == Role::HIGH && ctx.roleMap.high == ctx.roleMap.low)
        {
            pitch += 5;
            gainScale = 0.7f;
            if (gate <= 0.0 || gate > 0.75) gate = 0.75;
        }
        else if (slot == ctx.roleMap.low && e.role == Role::MID && ctx.roleMap.mid == ctx.roleMap.low)
        {
            pitch += 2;
            gainScale = 0.8f;
            if (gate <= 0.0 || gate > 1.5) gate = 1.5;
        }

        // Playback rate: pitch shift plus source-rate conversion.
        const double rate = std::pow (2.0, pitch / 12.0)
                          * (sample->sourceSampleRate / ctx.sampleRate);

        const int srcLen = sample->buffer.getNumSamples();
        int renderLen = (int) ((double) srcLen / rate);
        if (gate > 0.0)
            renderLen = juce::jmin (renderLen, (int) (gate * stepSec * ctx.sampleRate));
        renderLen = juce::jmin (renderLen, loopLen - startSample);   // truncate at loop end
        if (renderLen <= fadeLen / 2)
            continue;

        // Perceived-velocity gain curve: quiet ghosts stay audible.
        const float gain = (0.2f + 0.8f * std::pow (e.velocity, 1.5f)) * gainScale;

        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = juce::jmin (ch, sample->buffer.getNumChannels() - 1);
            const float* src = sample->buffer.getReadPointer (srcCh);
            float* dst = out.getWritePointer (ch) + startSample;

            for (int i = 0; i < renderLen; ++i)
            {
                double srcPos = i * rate;
                if (e.reverse)
                    srcPos = (double) (srcLen - 1) - srcPos;
                if (srcPos < 0.0 || srcPos >= srcLen - 1)
                    break;

                float v = readInterp (src, srcLen, srcPos) * gain;
                // Anti-click fades on both edges of every edit.
                if (i < fadeLen)
                    v *= (float) i / (float) fadeLen;
                const int fromEnd = renderLen - 1 - i;
                if (fromEnd < fadeLen)
                    v *= (float) fromEnd / (float) fadeLen;
                dst[i] += v;
            }
        }
    }

    // Loop boundary fade so the seam never clicks, whatever rang into it.
    const int edgeFade = juce::jmin (fadeLen, loopLen / 8);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* d = out.getWritePointer (ch);
        for (int i = 0; i < edgeFade; ++i)
        {
            d[loopLen - 1 - i] *= (float) i / (float) edgeFade;
            d[i] *= juce::jmin (1.0f, (float) i / (float) edgeFade + 0.5f);
        }
    }

    // Headroom: one clean gain to keep the true peak at or below -1 dBFS.
    const float peak = out.getMagnitude (0, loopLen);
    const float ceiling = juce::Decibels::decibelsToGain (-1.0f);
    if (peak > ceiling && peak > 0.0f)
        out.applyGain (ceiling / peak);

    return out;
}

} // namespace orcha
