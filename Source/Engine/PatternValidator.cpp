#include "PatternValidator.h"
#include <algorithm>
#include <map>

namespace orcha
{

Pattern PatternValidator::validate (Pattern p)
{
    const int steps = p.stepCount();
    const double lastAllowed = steps - 0.25;   // nothing may start in the final 32nd

    p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
        [&] (const Event& e) { return e.pos < 0.0 || e.pos > lastAllowed
                                      || e.velocity <= 0.0f; }),
        p.events.end());

    // Deduplicate (role, quantized 32nd) - keep the louder hit.
    std::map<std::pair<int, int>, size_t> seen;
    std::vector<Event> kept;
    kept.reserve (p.events.size());
    for (const auto& e : p.events)
    {
        const auto key = std::make_pair ((int) e.role, juce::roundToInt (e.pos * 2.0));
        auto it = seen.find (key);
        if (it == seen.end())
        {
            seen[key] = kept.size();
            kept.push_back (e);
        }
        else if (e.velocity > kept[it->second].velocity)
            kept[it->second] = e;
    }
    p.events = std::move (kept);

    // Readability cap: even max density must not exceed ~2.5 events per step.
    const size_t maxEvents = (size_t) (steps * 5 / 2);
    if (p.events.size() > maxEvents)
    {
        std::stable_sort (p.events.begin(), p.events.end(),
            [] (const Event& a, const Event& b)
            { return (a.protectedAnchor ? 1 : 0) + a.velocity
                   > (b.protectedAnchor ? 1 : 0) + b.velocity; });
        p.events.resize (maxEvents);
    }

    // BREAK must breathe: thin the quietest ghosts until enough steps are empty.
    if (p.settings.mode == Mode::BREAK)
    {
        auto occupiedSteps = [&p]
        {
            std::vector<bool> occ ((size_t) p.stepCount(), false);
            for (const auto& e : p.events)
                occ[(size_t) juce::jlimit (0, p.stepCount() - 1, (int) e.pos)] = true;
            return (int) std::count (occ.begin(), occ.end(), true);
        };
        while (occupiedSteps() > steps * 55 / 100 && ! p.events.empty())
        {
            auto quietest = std::min_element (p.events.begin(), p.events.end(),
                [] (const Event& a, const Event& b)
                {
                    if (a.protectedAnchor != b.protectedAnchor)
                        return ! a.protectedAnchor;
                    return a.velocity < b.velocity;
                });
            if (quietest->protectedAnchor)
                break;
            p.events.erase (quietest);
        }
    }

    // Never return silence, and DROP always states its downbeat.
    const bool hasDownbeatLow = std::any_of (p.events.begin(), p.events.end(),
        [] (const Event& e) { return e.pos < 0.26 && e.role == Role::LOW; });
    if (p.events.empty() || (p.settings.mode == Mode::DROP && ! hasDownbeatLow))
    {
        Event e;
        e.pos = 0.0;
        e.role = Role::LOW;
        e.velocity = 1.0f;
        e.protectedAnchor = true;
        p.events.insert (p.events.begin(), e);
    }

    std::sort (p.events.begin(), p.events.end(),
               [] (const Event& a, const Event& b) { return a.pos < b.pos; });
    return p;
}

} // namespace orcha
