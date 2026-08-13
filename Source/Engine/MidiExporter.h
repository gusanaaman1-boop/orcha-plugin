#pragma once

#include "Pattern.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace orcha
{

// Writes a Pattern as a standard MIDI file so the rhythm can be edited in the
// host with the user's own sounds. Roles map to General MIDI drum notes:
// LOW=36 (kick), MID=38 (snare), HIGH=42 (closed hat), FX=49 (crash).
// Swing and humanization are baked into the note times, exactly as rendered.
namespace MidiExporter
{
    int noteForRole (Role role);

    // Deterministic sibling of RenderCache::fileFor, .mid extension.
    juce::File fileFor (const Pattern& p, double bpm);

    // Returns an invalid File on failure. Worker or message thread.
    juce::File write (const Pattern& p, double bpm);

    // The favorites chain as ONE MIDI file: each pattern starts on the bar
    // right after the previous one, exactly like the chained WAV. Written to
    // `file` (overwritten); returns an invalid File on failure.
    juce::File writeChain (const std::vector<const Pattern*>& patterns,
                           double bpm, const juce::File& file);
}

} // namespace orcha
