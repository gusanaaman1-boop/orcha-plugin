#pragma once

#include "Pattern.h"
#include <vector>

namespace orcha
{

// Offline render of a Pattern into an audio loop at a given host tempo and
// sample rate. Pure function of its inputs - worker threads only.
namespace LoopRenderer
{
    struct Context
    {
        double sampleRate = 48000.0;
        double bpm = 120.0;
        std::vector<InputSample::Ptr> samples;   // slot-indexed, nulls allowed
        RoleMap roleMap;
        // Global PITCH switch: off mutes every musical pitch move (countdown,
        // rolls, randomness). The single-sample role-separation offsets stay -
        // they are timbre, not melody.
        bool pitchEnabled = true;
    };

    // Stereo buffer, exactly bars * 4 beats long at ctx.bpm/ctx.sampleRate.
    // Anti-click fades at every edit and at the loop boundary; peak limited
    // to -1 dBFS with headroom preserved (one clean gain, no compression).
    juce::AudioBuffer<float> render (const Pattern& pattern, const Context& ctx);

    // Several patterns as ONE phrase, played the way a host would play them
    // back to back. Each card's reverb/delay tail spills FORWARD into the card
    // that follows it instead of wrapping into its own start, and the last
    // card's tail wraps round to the top so the whole phrase still loops.
    // Returns an empty buffer for fewer than two patterns.
    juce::AudioBuffer<float> renderChain (const std::vector<const Pattern*>& patterns,
                                          const Context& ctx);
}

} // namespace orcha
