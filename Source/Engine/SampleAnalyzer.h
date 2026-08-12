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
}

} // namespace orcha
