#pragma once

#include "Pattern.h"
#include <vector>

namespace orcha
{

// The functional job of each phrase segment, decided BEFORE any event is
// placed. Segments are bars (2/4-bar loops) or beats (1-bar loops).
enum class PhraseRole
{
    Impact,       // state the section with full weight
    Establish,    // state the motif plainly
    Lock,         // hold the motif steady
    Develop,      // vary the motif, keep its identity
    Lift,         // add motion upward
    Call,         // pose the question
    Response,     // answer with related material
    Contrast,     // the B section - different lead, thinner anchors
    Accelerate,   // tighten intervals, raise everything
    Turnaround,   // steer back to the loop start
    Breath,       // planned air
    Vacuum,       // near-total silence before the next impact
    Resolve       // land firmly
};

const char* phraseRoleName (PhraseRole r);

struct PhrasePlan
{
    // One role per segment. size == bars for multi-bar loops, 4 (beats) for
    // a one-bar loop.
    std::vector<PhraseRole> roles;
    bool beatLevel = false;    // true for 1-bar loops

    int segments() const { return (int) roles.size(); }

    // Which segment a step position belongs to.
    int segmentOf (double pos, int stepCount) const
    {
        if (roles.empty())
            return 0;
        const int seg = beatLevel ? (int) (pos / 4.0)
                                  : (int) (pos / 16.0);
        juce::ignoreUnused (stepCount);
        return juce::jlimit (0, segments() - 1, seg);
    }
};

// Deterministic: same (mode, bars, motifSeed) -> same plan, always. The plan
// is reproducible entirely from saved seeds, so it needs no serialization.
namespace PhrasePlanner
{
    PhrasePlan plan (Mode mode, int bars, juce::uint64 motifSeed);
}

} // namespace orcha
