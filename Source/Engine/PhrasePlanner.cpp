#include "PhrasePlanner.h"

namespace orcha
{

const char* phraseRoleName (PhraseRole r)
{
    switch (r)
    {
        case PhraseRole::Impact:     return "Impact";
        case PhraseRole::Establish:  return "Establish";
        case PhraseRole::Lock:       return "Lock";
        case PhraseRole::Develop:    return "Develop";
        case PhraseRole::Lift:       return "Lift";
        case PhraseRole::Call:       return "Call";
        case PhraseRole::Response:   return "Response";
        case PhraseRole::Contrast:   return "Contrast";
        case PhraseRole::Accelerate: return "Accelerate";
        case PhraseRole::Turnaround: return "Turnaround";
        case PhraseRole::Breath:     return "Breath";
        case PhraseRole::Vacuum:     return "Vacuum";
        case PhraseRole::Resolve:    return "Resolve";
    }
    return "Establish";
}

PhrasePlan PhrasePlanner::plan (Mode mode, int bars, juce::uint64 motifSeed)
{
    // Only the grammar's documented choice points consume randomness, and
    // only from the motif stream - the plan is part of the groove's identity.
    juce::Random rng ((juce::int64) (motifSeed ^ 0x9A7A9A7Aull));
    auto pick = [&rng] (PhraseRole a, PhraseRole b, float pA)
    {
        return rng.nextFloat() < pA ? a : b;
    };

    PhrasePlan p;
    if (bars >= 4)
    {
        switch (mode)
        {
            case Mode::DROP:
                p.roles = { PhraseRole::Impact, PhraseRole::Lock,
                            pick (PhraseRole::Lift, PhraseRole::Develop, 0.5f),
                            pick (PhraseRole::Turnaround, PhraseRole::Resolve, 0.6f) };
                break;
            case Mode::BREAK:
                p.roles = { PhraseRole::Establish, PhraseRole::Call,
                            PhraseRole::Response, PhraseRole::Breath };
                break;
            case Mode::BUILD:
                p.roles = { PhraseRole::Establish, PhraseRole::Develop,
                            PhraseRole::Accelerate,
                            pick (PhraseRole::Vacuum, PhraseRole::Resolve, 0.45f) };
                break;
            case Mode::GROOVE:
                p.roles = { PhraseRole::Establish, PhraseRole::Develop,
                            PhraseRole::Contrast, PhraseRole::Turnaround };
                break;
        }
    }
    else if (bars == 2)
    {
        switch (mode)
        {
            case Mode::DROP:
                p.roles = { PhraseRole::Impact,
                            pick (PhraseRole::Turnaround, PhraseRole::Develop, 0.6f) };
                break;
            case Mode::BREAK:
                p.roles = { PhraseRole::Call, PhraseRole::Response };
                break;
            case Mode::BUILD:
                p.roles = { PhraseRole::Develop, PhraseRole::Accelerate };
                break;
            case Mode::GROOVE:
                p.roles = { PhraseRole::Establish, PhraseRole::Turnaround };
                break;
        }
    }
    else
    {
        // One bar, planned at beat level: still a miniature statement,
        // development and return.
        p.beatLevel = true;
        switch (mode)
        {
            case Mode::DROP:
                p.roles = { PhraseRole::Impact, PhraseRole::Lock,
                            PhraseRole::Develop, PhraseRole::Turnaround };
                break;
            case Mode::BREAK:
                p.roles = { PhraseRole::Establish, PhraseRole::Call,
                            PhraseRole::Response, PhraseRole::Breath };
                break;
            case Mode::BUILD:
                p.roles = { PhraseRole::Establish, PhraseRole::Develop,
                            PhraseRole::Accelerate,
                            pick (PhraseRole::Vacuum, PhraseRole::Resolve, 0.4f) };
                break;
            case Mode::GROOVE:
                p.roles = { PhraseRole::Establish, PhraseRole::Lock,
                            PhraseRole::Develop, PhraseRole::Turnaround };
                break;
        }
    }
    return p;
}

} // namespace orcha
