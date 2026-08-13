#include "LoopRenderer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>

namespace orcha
{

namespace
{
    // The effects themselves, straight through the given buffer: whatever
    // tail they generate lands wherever the buffer still has room. Callers
    // decide what that room means - a second loop pass (a loop that must be
    // seamless) or trailing silence (a card inside a chain, whose tail
    // belongs to the card that follows it).
    void processFx (juce::AudioBuffer<float>& twice, double sampleRate, double bpm,
                    float reverbAmt, float delayAmt)
    {
        const bool reverb = reverbAmt > 0.01f;
        const bool delay = delayAmt > 0.01f;

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
    }

    // Per-card effects baked into a loop that has to stay seamless. The trick:
    // process TWO passes of the loop and keep the second - the first pass's
    // tail wraps into it, so the loop start already carries the reverb/delay
    // of the loop end.
    void applyLoopFx (juce::AudioBuffer<float>& loop, double sampleRate, double bpm,
                      float reverbAmt, float delayAmt)
    {
        reverbAmt = juce::jlimit (0.0f, 1.0f, reverbAmt);
        delayAmt = juce::jlimit (0.0f, 1.0f, delayAmt);
        if (reverbAmt <= 0.01f && delayAmt <= 0.01f)
            return;

        const int len = loop.getNumSamples();
        juce::AudioBuffer<float> twice (2, len * 2);
        for (int ch = 0; ch < 2; ++ch)
        {
            twice.copyFrom (ch, 0, loop, ch, 0, len);
            twice.copyFrom (ch, len, loop, ch, 0, len);
        }
        processFx (twice, sampleRate, bpm, reverbAmt, delayAmt);
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

    // F1: the pump. A gain envelope carved by the loop's own LOW hits -
    // deterministic, click-free, sample-rate independent. Depth follows the
    // amount; attack is instant-but-smoothed, release ~140 ms curve.
    if (pattern.fxPump > 0.01f)
    {
        const float depth = juce::jlimit (0.0f, 1.0f, pattern.fxPump) * 0.8f;
        const int releaseLen = (int) (ctx.sampleRate * 0.14);
        const int attackLen = juce::jmax (8, (int) (ctx.sampleRate * 0.004));
        std::vector<float> env ((size_t) loopLen, 1.0f);
        for (const auto& e : pattern.events)
        {
            if (e.role != Role::LOW || e.velocity < 0.5f)
                continue;
            const int at = (int) (e.pos * stepSec * ctx.sampleRate);
            for (int i = 0; i < attackLen + releaseLen; ++i)
            {
                const int idx = (at + i) % loopLen;   // wraps: the loop pumps seamlessly
                float g;
                if (i < attackLen)
                    g = 1.0f - depth * (float) i / (float) attackLen;
                else
                {
                    const float t = (float) (i - attackLen) / (float) releaseLen;
                    g = 1.0f - depth * (1.0f - t) * (1.0f - t);
                }
                env[(size_t) idx] = juce::jmin (env[(size_t) idx], g);
            }
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = out.getWritePointer (ch);
            for (int i = 0; i < loopLen; ++i)
                d[i] *= env[(size_t) i];
        }
    }

    // Headroom: one clean gain to keep the true peak at or below -1 dBFS.
    const float peak = out.getMagnitude (0, loopLen);
    const float ceiling = juce::Decibels::decibelsToGain (-1.0f);
    if (peak > ceiling && peak > 0.0f)
        out.applyGain (ceiling / peak);

    return out;
}

juce::AudioBuffer<float> LoopRenderer::renderChain (
    const std::vector<const Pattern*>& patterns, const Context& ctx)
{
    if (patterns.size() < 2)
        return { 2, 0 };

    // Every card rendered DRY: its own pump and its own headroom gain stay,
    // but reverb and delay are held back so they can be applied here, on the
    // chain timeline, where they have somewhere to spill to.
    std::vector<juce::AudioBuffer<float>> dry;
    std::vector<int> offset;
    int total = 0;
    for (const auto* p : patterns)
    {
        Pattern noFx = *p;
        noFx.fxReverb = 0.0f;
        noFx.fxDelay = 0.0f;
        dry.push_back (render (noFx, ctx));
        offset.push_back (total);
        total += dry.back().getNumSamples();
    }
    if (total <= 0)
        return { 2, 0 };

    // Room for the last tail to decay before it wraps back to the top. Two
    // seconds covers the largest room and the dotted-8th feedback, and never
    // more than the phrase itself, so the wrap can never overlap twice.
    const int tail = juce::jmin ((int) (ctx.sampleRate * 2.0), total / 2);

    juce::AudioBuffer<float> chain (2, total + tail);
    chain.clear();
    juce::AudioBuffer<float> work (2, 0);
    for (size_t i = 0; i < dry.size(); ++i)
    {
        const int len = dry[i].getNumSamples();
        const float r = juce::jlimit (0.0f, 1.0f, patterns[i]->fxReverb);
        const float d = juce::jlimit (0.0f, 1.0f, patterns[i]->fxDelay);
        if (r <= 0.01f && d <= 0.01f)
        {
            for (int ch = 0; ch < 2; ++ch)
                chain.addFrom (ch, offset[i], dry[i], ch, 0, len);
            continue;
        }
        // The card plus empty room after it: the effect decays into that room,
        // and the whole thing is added on top of whatever comes next.
        work.setSize (2, len + tail, false, false, true);
        work.clear();
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, dry[i], ch, 0, len);
        processFx (work, ctx.sampleRate, ctx.bpm, r, d);
        for (int ch = 0; ch < 2; ++ch)
            chain.addFrom (ch, offset[i], work, ch, 0,
                           juce::jmin (work.getNumSamples(), chain.getNumSamples() - offset[i]));
    }

    // Fold the overhanging tail back onto the phrase start: the chain loops.
    juce::AudioBuffer<float> out (2, total);
    for (int ch = 0; ch < 2; ++ch)
    {
        out.copyFrom (ch, 0, chain, ch, 0, total);
        out.addFrom (ch, 0, chain, ch, total, tail);
    }

    const float peak = out.getMagnitude (0, total);
    const float ceiling = juce::Decibels::decibelsToGain (-1.0f);
    if (peak > ceiling && peak > 0.0f)
        out.applyGain (ceiling / peak);
    return out;
}

} // namespace orcha
