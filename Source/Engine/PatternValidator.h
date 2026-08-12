#pragma once

#include "Pattern.h"

namespace orcha
{

// Musical guard-rails applied after generation. Deterministic: fixes are rule
// based, so a validated pattern is still exactly reproducible from its seed.
namespace PatternValidator
{
    // Returns the cleaned pattern. Guarantees:
    //  - every event inside [0, stepCount), nothing starting in the final 32nd
    //  - no duplicate (role, quantized position) pairs
    //  - at least one event; DROP always keeps its downbeat LOW
    //  - BREAK keeps a real silence ratio (<= 55% of steps occupied)
    //  - total event count capped so high density stays readable
    Pattern validate (Pattern p);
}

} // namespace orcha
