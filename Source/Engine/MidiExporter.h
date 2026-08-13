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
}

} // namespace orcha
