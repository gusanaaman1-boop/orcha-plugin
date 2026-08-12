#pragma once

#include "Pattern.h"
#include "RhythmStyle.h"
#include "SectionProfile.h"

namespace orcha
{

// Deterministic seeded pattern generation. Same (seed, settings) -> identical
// Pattern, always; all variation between the 12 options comes from their seeds.
namespace LoopGenerator
{
    Pattern generate (juce::uint64 seed, const GeneratorSettings& settings);

    // Derives the per-option seed stream from a master seed. Splitmix64: good
    // dispersion, stable across platforms.
    juce::uint64 deriveSeed (juce::uint64 master, int optionIndex);
}

} // namespace orcha
