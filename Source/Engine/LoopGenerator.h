#pragma once

#include "Pattern.h"
#include "RhythmStyle.h"
#include "SectionProfile.h"

namespace orcha
{

// Deterministic seeded pattern generation. Same (motifSeed, ornamentSeed,
// settings) -> identical Pattern, always; all variation between the 12
// options comes from their seeds.
//
// The two seeds split the decisions: motifSeed picks the skeleton, the lead
// role and the anchor layout - the identity of the groove. ornamentSeed
// drives ornaments, randomness, humanization, fills. Hold the motif seed and
// re-roll the ornament seed to get "the same groove, another take".
namespace LoopGenerator
{
    // ENGINE 1 - FROZEN. Old projects restore through this path bit-for-bit;
    // a characterization hash in the test suite guards it. Do not change its
    // random draw order or behavior.
    Pattern generate (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                      const GeneratorSettings& settings);

    // ENGINE 2 - phrase-planned generation (FeelVector + PhrasePlanner).
    // Same determinism contract; used for all NEW generations. The traits
    // tell the SYMBOLIC stage what the samples can carry (sustained needs
    // spacing, weak transients cannot roll) - same traits + same seeds =
    // same pattern.
    Pattern generateV2 (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                        const GeneratorSettings& settings,
                        const TraitsByRole& traits,
                        Destination destination = Destination::LoopBack);
    Pattern generateV2 (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                        const GeneratorSettings& settings);

    // Convenience for callers that want one knob: ornament seed derived from
    // the motif seed.
    Pattern generate (juce::uint64 seed, const GeneratorSettings& settings);

    // Derives the per-option seed stream from a master seed. Splitmix64: good
    // dispersion, stable across platforms.
    juce::uint64 deriveSeed (juce::uint64 master, int optionIndex);

    // Phase A7 - CLEAN: deterministic decoration stripping in three
    // strengths. 1 = ghosts and graces; 2 = also quiet ornaments and soft
    // rolls; 3 = back to skeleton + strong motif. Anchors always survive,
    // and removal can never violate planned silence (it only removes).
    void cleanPattern (Pattern& p, int strength);

    // Phase A8 - ENDING override: rewrite ONLY the transition region of an
    // existing pattern to the given destination. Deterministic via
    // choiceSeed. Used by generateV2 itself and by the EDIT panel when the
    // user forces an ending on an edited card.
    void applyEnding (Pattern& p, Destination destination,
                      const TraitsByRole& traits, juce::uint64 choiceSeed);
}

} // namespace orcha
