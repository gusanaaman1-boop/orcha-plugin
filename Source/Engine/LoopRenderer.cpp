#include "LoopRenderer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>

namespace orcha
{

namespace
{
    // Gentle, fixed-amount per-card effects, baked into the loop. The trick
    // that keeps the loop seamless: process TWO passes of the loop through
    // the effect and keep the second - the first pass's tail wraps into it,
    // so the loop start already carries the reverb/delay of the loop end.
    void applyLoopFx (juce::AudioBuffer<float>& loop, double sampleRate, double bpm,
                      float reverbAmt, float delayAmt)
    {
        reverbAmt = juce::jlimit (0.0f, 1.0f, reverbAmt);
        delayAmt = juce::jlimit (0.0f, 1.0f, delayAmt);
        const bool reverb = reverbAmt > 0.01f;
        const bool delay = delayAmt > 0.01f;
        if (! reverb && ! delay)
            return;

        const int len = loop.getNumSamples();
        juce::AudioBuffer<float> twice (2, len * 2);
        for (int ch = 0; ch < 2; ++ch)
        {
            twice.copyFrom (ch, 0, loop, ch, 0, len);
            twice.copyFrom (ch, len, loop, ch, 0, len);
        }

        if (delay)
        {
            // Dotted-8th feedback delay, high-passed so the lows stay clean.
            const int delaySamples = juce::jmax (1,
                (int) (0.75 * (60.0 / bpm) * sampleRate));
            std::vector<float> dl ((size_t) delaySamples * 2, 0.0f);
            float hp0 = 0.0f, hp1 = 0.0f;
            size_t pos = 0;
            // The amount drives both how loud the echoes are and how long
            // they regenerate.
            const float feedback = 0.2f + 0.3f * delayAmt;
            const float wet = 0.32f * delayAmt;
            for (int i = 0; i < twice.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    float& hp = ch == 0 ? hp0 : hp1;
                    float* d = twice.getWritePointer (ch);
                    const size_t idx = pos * 2 + (size_t) ch;
                    const float echo = dl[idx];
                    // One-pole highpass at ~220 Hz inside the loop.
                    hp += (echo - hp) * (float) (220.0 * juce::MathConstants<double>::twoPi
                                                 / sampleRate);
                    const float echoHp = echo - hp;
                    dl[idx] = d[i] + echoHp * feedback;
                    d[i] += echoHp * wet;
                    if (ch == 1)
                        pos = (pos + 1) % (size_t) delaySamples;
                }
        }

        if (reverb)
        {
            juce::Reverb verb;
            verb.setSampleRate (sampleRate);
            juce::Reverb::Parameters params;
            params.roomSize = 0.35f + 0.3f * reverbAmt;
            params.damping = 0.5f;
            params.wetLevel = 0.3f * reverbAmt;   // halo at low, wash at full
            params.dryLevel = 1.0f - 0.25f * reverbAmt;
            params.width = 1.0f;
            verb.setParameters (params);
            verb.processStereo (twice.getWritePointer (0), twice.getWritePointer (1),
                                twice.getNumSamples());
        }

        for (int ch = 0; ch < 2; ++ch)
            loop.copyFrom (ch, 0, twice, ch, len, len);
    }
}

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

    // Pass 1: final start time of every event (swing + humanization applied).
    // Needed up front so hats can choke: a HIGH hit must silence the previous
    // HIGH hit, like a closed hat stopping an open one.
    std::vector<int> starts (pattern.events.size(), -1);
    for (size_t i = 0; i < pattern.events.size(); ++i)
    {
        const auto& e = pattern.events[i];
        const bool oddStep = (juce::roundToInt (std::floor (e.pos)) % 2) == 1
                             && std::abs (e.pos - std::floor (e.pos)) < 0.01;
        double timeSec = e.pos * stepSec
                       + (oddStep ? pattern.swing * stepSec * 0.5 : 0.0)
                       + e.microMs * 0.001;
        if (timeSec < 0.0)
            timeSec = 0.0;
        starts[i] = (int) (timeSec * ctx.sampleRate);
    }

    for (size_t eventIndex = 0; eventIndex < pattern.events.size(); ++eventIndex)
    {
        const auto& e = pattern.events[eventIndex];
        const int slot = ctx.roleMap.slotFor (e.role);
        if (slot < 0 || slot >= (int) ctx.samples.size())
            continue;
        const auto& sample = ctx.samples[(size_t) slot];
        if (sample == nullptr || sample->buffer.getNumSamples() == 0)
            continue;

        const int startSample = starts[eventIndex];
        if (startSample >= loopLen)
            continue;

        // One loaded sample covering several roles still needs the roles to
        // sound different: non-LOW roles borrow pitch and a shorter gate.
        int pitch = ctx.pitchEnabled ? e.pitchSemis : 0;
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

        // Hat choke: a HIGH hit ends when the next HIGH hit starts, plus a
        // short crossfade - the open/closed hi-hat behaviour that keeps busy
        // top lines readable. Reversed swells are transitions, not hats -
        // they ride over the line unchoked.
        if (e.role == Role::HIGH && ! e.reverse)
            for (size_t j = 0; j < pattern.events.size(); ++j)
                if (pattern.events[j].role == Role::HIGH && starts[j] > startSample)
                    renderLen = juce::jmin (renderLen, starts[j] - startSample + fadeLen);

        renderLen = juce::jmin (renderLen, loopLen - startSample);   // truncate at loop end
        if (renderLen <= fadeLen / 2)
            continue;

        // Perceived-velocity gain curve: quiet ghosts stay audible.
        const float gain = (0.2f + 0.8f * std::pow (e.velocity, 1.5f)) * gainScale;

        // Accent = brightness, not just volume. Accented hits get a decaying
        // first-difference "snap" over the first 30 ms; ghosts lean the other
        // way, slightly rounded off, so they sit behind the accents.
        const float accentAmt = e.velocity > 0.6f ? (e.velocity - 0.6f) * 1.1f : 0.0f;
        const float ghostAmt = e.velocity < 0.35f ? (0.35f - e.velocity) * 1.2f : 0.0f;
        const int snapLen = juce::jmax (1, (int) (ctx.sampleRate * 0.03));

        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = juce::jmin (ch, sample->buffer.getNumChannels() - 1);
            const float* src = sample->buffer.getReadPointer (srcCh);
            float* dst = out.getWritePointer (ch) + startSample;
            float prev = 0.0f;

            for (int i = 0; i < renderLen; ++i)
            {
                double srcPos = i * rate;
                if (e.reverse)
                    srcPos = (double) (srcLen - 1) - srcPos;
                if (srcPos < 0.0 || srcPos >= srcLen - 1)
                    break;

                float v = readInterp (src, srcLen, srcPos);
                const float diff = v - prev;
                prev = v;
                if (accentAmt > 0.0f && i < snapLen)
                    v += diff * accentAmt * (1.0f - (float) i / (float) snapLen);
                else if (ghostAmt > 0.0f)
                    v -= diff * ghostAmt * 0.4f;

                v *= gain;
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

    // Per-card polish, after the dry loop is final and before the headroom
    // pass, so the ceiling still holds with the effect in.
    applyLoopFx (out, ctx.sampleRate, ctx.bpm, pattern.fxReverb, pattern.fxDelay);

    // Headroom: one clean gain to keep the true peak at or below -1 dBFS.
    const float peak = out.getMagnitude (0, loopLen);
    const float ceiling = juce::Decibels::decibelsToGain (-1.0f);
    if (peak > ceiling && peak > 0.0f)
        out.applyGain (ceiling / peak);

    return out;
}

} // namespace orcha
