#pragma once

#include "../Model/InputSample.h"
#include "Pattern.h"
#include <vector>

namespace orcha
{

// Offline analysis of a loaded sample and role assignment across the loaded
// set. Runs on worker threads only.
namespace SampleAnalyzer
{
    SampleAnalysis analyze (const juce::AudioBuffer<float>& buffer, double sampleRate);

    // Assigns complementary roles to the loaded samples (honouring any manual
    // userRole) and returns the role map the generator/renderer will use.
    // `samples` may contain nulls for empty slots.
    RoleMap assignRoles (std::vector<InputSample::Ptr>& samples);

    // Phase E - kit slicing. Onset positions (in samples) of a loop/stem,
    // envelope-based, minimum 90 ms apart, threshold relative to the file's
    // own level. Deterministic.
    std::vector<int> detectOnsets (const juce::AudioBuffer<float>& buffer,
                                   double sampleRate);

    // Picks representative LOW / MID / HIGH slices from the onsets, as
    // [start,end) fractions of the buffer - ready to feed the existing
    // non-destructive cut mechanism, which also makes them persist in state
    // for free. Returns fewer than 3 pairs when confidence is low; the
    // caller must NOT pretend otherwise.
    struct KitSlice { float start = 0.0f, end = 1.0f; };
    std::vector<KitSlice> chooseKitSlices (const juce::AudioBuffer<float>& buffer,
                                           double sampleRate);
}

} // namespace orcha
