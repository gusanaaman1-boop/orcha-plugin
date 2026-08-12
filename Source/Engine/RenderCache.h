#pragma once

#include "Pattern.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace orcha
{

// Rendered-loop WAV files, named by everything that determines their audio, so
// a cache hit is always valid and a settings change is always a miss.
namespace RenderCache
{
    juce::File cacheDirectory();

    // Deterministic file for (pattern seed+settings, bpm, sampleRate).
    juce::File fileFor (const Pattern& p, double bpm, double sampleRate);

    // Writes a 24-bit WAV; returns an invalid File on failure. Safe to call
    // from worker threads.
    juce::File write (const juce::AudioBuffer<float>& buffer, const Pattern& p,
                      double bpm, double sampleRate);

    // Drops cache files older than a week so the directory cannot grow forever.
    void cleanupStale();
}

} // namespace orcha
