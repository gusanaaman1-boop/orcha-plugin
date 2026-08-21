#pragma once

#include "Pattern.h"

namespace orcha
{

// A curated one-bar fill vocabulary, in two families:
//   PLAYED  - fills that move like a drummer behind the kit: ramps, trades,
//             cascades, crescendos.
//   CHOPPED - fills that treat the kit like material: stutters, gate chops,
//             reverse suction, pitch stairs.
// Every hit is the user's own samples - ORCHA's standing rule - so a fill
// always sounds like the track it is going into.
//
// Template choice comes from the MOTIF seed (reroll = same fill, another
// take); velocities, micro-timing and the macro knobs shade it from the
// ornament stream. Deterministic, like everything else in the engine.
namespace FillBank
{
    int templateCount();

    // A one-bar Pattern (algo 2, bars forced to 1). `settings.mode` is
    // expected to be Mode::FILL; energy/density/randomness apply.
    Pattern build (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                   const GeneratorSettings& settings, const TraitsByRole& traits);
}

} // namespace orcha
