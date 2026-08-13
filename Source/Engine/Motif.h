#pragma once

#include "Pattern.h"
#include "PhrasePlanner.h"
#include <vector>

namespace orcha
{

// The motif as a first-class musical object: the decorated cell one segment
// states, which the rest of the phrase develops through explicit,
// style-constrained transformations - not through random mutation.
//
// A Cell is a lightweight view over the existing Event type (positions
// normalized to the segment start), so nothing duplicates the event model.
namespace Motif
{
    struct Cell
    {
        std::vector<Event> events;   // pos in [0, segLen)
        double segLen = 16.0;
    };

    enum class Transform
    {
        ExactRepeat = 0,
        DelayedRepeat,     // the same cell, half a step later
        Anticipation,      // the same cell, pulled ahead
        Truncate,          // only the first half survives
        Extend,            // the last gesture continues one step further
        OmitLast,          // the phrase-comma: drop the final event
        DensifyEnd,        // the ending doubles into itself
        EchoSofter,        // the same cell as a quieter memory
        RoleSubstitute,    // mid/high voices trade seats
        Displace,          // whole cell shifted one step (families that allow it)
        QuestionSilence,   // the ending is withheld - a question
        AnswerLowResolve   // softer echo that lands on a LOW resolution
    };

    const char* transformName (Transform t);

    // Which transformations a family's grammar permits. Arabic keeps its
    // iqa' positions (no displacement); psytrance keeps its precision (no
    // displacement/anticipation); cinematic keeps its space.
    const std::vector<Transform>& allowedFor (Family family);

    // The conservative-to-adventurous transform menu of each phrase role.
    const std::vector<Transform>& menuFor (PhraseRole role);

    // Deterministic choice: low randomness picks from the front of the menu
    // (conservative), high randomness reaches deeper - always inside the
    // family's allowed set. Draws exactly one value from rng.
    Transform choose (PhraseRole role, Family family, float randomness,
                      juce::Random& rng);

    // Apply a transformation. Deterministic; draws from rng only where the
    // transform itself has freedom (e.g. which voice substitutes).
    Cell apply (const Cell& cell, Transform t, juce::Random& rng);

    // Meter-aware similarity in [0,1]: coincidences on beats weigh far more
    // than weak subdivisions, and role agreement matters.
    float similarity (const Cell& a, const Cell& b);

    // Extract the motif-layer events of one segment of a finished pattern
    // (the decorated cell: everything that is not an anchor, ghost or roll).
    Cell extract (const Pattern& p, int segment, double segLen);

    // The explicit call/response relationship of a plan, if it has one.
    struct CallResponsePlan
    {
        int callSegment = -1;
        int responseSegment = -1;
        Transform relation = Transform::EchoSofter;
        float targetSimilarity = 0.7f;   // response must stay recognizable
        bool valid() const { return callSegment >= 0; }
    };

    CallResponsePlan callResponseOf (const PhrasePlan& plan, Family family,
                                     float randomness, juce::uint64 motifSeed);
}

} // namespace orcha
