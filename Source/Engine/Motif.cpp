#include "Motif.h"
#include <algorithm>
#include <cmath>

namespace orcha
{

const char* Motif::transformName (Transform t)
{
    switch (t)
    {
        case Transform::ExactRepeat:     return "ExactRepeat";
        case Transform::DelayedRepeat:   return "DelayedRepeat";
        case Transform::Anticipation:    return "Anticipation";
        case Transform::Truncate:        return "Truncate";
        case Transform::Extend:          return "Extend";
        case Transform::OmitLast:        return "OmitLast";
        case Transform::DensifyEnd:      return "DensifyEnd";
        case Transform::EchoSofter:      return "EchoSofter";
        case Transform::RoleSubstitute:  return "RoleSubstitute";
        case Transform::Displace:        return "Displace";
        case Transform::QuestionSilence: return "QuestionSilence";
        case Transform::AnswerLowResolve:return "AnswerLowResolve";
    }
    return "ExactRepeat";
}

const std::vector<Motif::Transform>& Motif::allowedFor (Family family)
{
    using T = Transform;
    // Grammar per family. What is NOT here is forbidden for that style.
    static const std::vector<T> arabic {
        T::ExactRepeat, T::DelayedRepeat, T::Truncate, T::OmitLast,
        T::DensifyEnd, T::EchoSofter, T::RoleSubstitute,
        T::QuestionSilence, T::AnswerLowResolve };
    static const std::vector<T> psy {
        T::ExactRepeat, T::EchoSofter, T::OmitLast, T::DensifyEnd,
        T::QuestionSilence };
    static const std::vector<T> afro {
        T::ExactRepeat, T::DelayedRepeat, T::Displace, T::RoleSubstitute,
        T::EchoSofter, T::Truncate, T::AnswerLowResolve };
    static const std::vector<T> cinematic {
        T::ExactRepeat, T::EchoSofter, T::OmitLast, T::Truncate,
        T::QuestionSilence, T::AnswerLowResolve };
    static const std::vector<T> breaks {
        T::ExactRepeat, T::DelayedRepeat, T::Anticipation, T::Truncate,
        T::Extend, T::OmitLast, T::DensifyEnd, T::EchoSofter,
        T::RoleSubstitute, T::Displace, T::QuestionSilence,
        T::AnswerLowResolve };
    static const std::vector<T> edm {
        T::ExactRepeat, T::DelayedRepeat, T::Anticipation, T::Truncate,
        T::Extend, T::OmitLast, T::DensifyEnd, T::EchoSofter, T::Displace };

    switch (family)
    {
        case Family::ARABIC:
        case Family::MEDITERRANEAN:  return arabic;
        case Family::PSYTRANCE:      return psy;
        case Family::AFRO:           return afro;
        case Family::CINEMATIC:      return cinematic;
        case Family::BREAKS:         return breaks;
        case Family::EDM:
        case Family::MELODIC_TECHNO:
        case Family::URBAN:
        case Family::HYBRID:         return edm;
    }
    return edm;
}

const std::vector<Motif::Transform>& Motif::menuFor (PhraseRole role)
{
    using T = Transform;
    static const std::vector<T> keep { T::ExactRepeat };
    static const std::vector<T> lock { T::ExactRepeat, T::EchoSofter };
    static const std::vector<T> develop { T::DelayedRepeat, T::Displace,
        T::Extend, T::RoleSubstitute, T::Anticipation };
    static const std::vector<T> lift { T::DensifyEnd, T::Extend, T::DelayedRepeat };
    static const std::vector<T> accelerate { T::DensifyEnd, T::Extend };
    static const std::vector<T> contrast { T::RoleSubstitute, T::Displace, T::Truncate };
    static const std::vector<T> call { T::Truncate, T::QuestionSilence, T::OmitLast };
    static const std::vector<T> response { T::AnswerLowResolve, T::EchoSofter,
        T::DelayedRepeat };
    static const std::vector<T> turnaround { T::OmitLast, T::DensifyEnd,
        T::Anticipation };
    static const std::vector<T> resolve { T::ExactRepeat, T::AnswerLowResolve };
    static const std::vector<T> breath { T::QuestionSilence };

    switch (role)
    {
        case PhraseRole::Impact:
        case PhraseRole::Establish:  return keep;
        case PhraseRole::Lock:       return lock;
        case PhraseRole::Develop:    return develop;
        case PhraseRole::Lift:       return lift;
        case PhraseRole::Accelerate: return accelerate;
        case PhraseRole::Contrast:   return contrast;
        case PhraseRole::Call:       return call;
        case PhraseRole::Response:   return response;
        case PhraseRole::Turnaround: return turnaround;
        case PhraseRole::Resolve:    return resolve;
        case PhraseRole::Breath:
        case PhraseRole::Vacuum:     return breath;
    }
    return keep;
}

Motif::Transform Motif::choose (PhraseRole role, Family family, float randomness,
                                juce::Random& rng)
{
    const auto& menu = menuFor (role);
    const auto& allowed = allowedFor (family);
    std::vector<Transform> pool;
    for (auto t : menu)
        if (std::find (allowed.begin(), allowed.end(), t) != allowed.end())
            pool.push_back (t);
    // One draw ALWAYS, so the caller's stream stays aligned whatever happens.
    const float u = rng.nextFloat();
    if (pool.empty())
        return Transform::ExactRepeat;
    // Low randomness stays at the front (conservative); high reaches deeper.
    const int reach = 1 + (int) (randomness * (float) (pool.size() - 1) + 0.5f);
    return pool[(size_t) juce::jmin ((int) pool.size() - 1, (int) (u * (float) reach))];
}

Motif::Cell Motif::apply (const Cell& cell, Transform t, juce::Random& rng)
{
    Cell out = cell;
    auto& ev = out.events;
    auto sortCell = [&ev]
    {
        std::sort (ev.begin(), ev.end(), eventBefore);
    };
    sortCell();

    switch (t)
    {
        case Transform::ExactRepeat:
            break;
        case Transform::DelayedRepeat:
            for (auto& e : ev) e.pos += 0.5;
            ev.erase (std::remove_if (ev.begin(), ev.end(),
                [&] (const Event& e) { return e.pos >= cell.segLen - 0.25; }), ev.end());
            break;
        case Transform::Anticipation:
            for (auto& e : ev) e.pos -= 0.5;
            ev.erase (std::remove_if (ev.begin(), ev.end(),
                [] (const Event& e) { return e.pos < 0.0; }), ev.end());
            break;
        case Transform::Truncate:
            ev.erase (std::remove_if (ev.begin(), ev.end(),
                [&] (const Event& e) { return e.pos >= cell.segLen * 0.5; }), ev.end());
            break;
        case Transform::Extend:
            if (! ev.empty())
            {
                Event tail = ev.back();
                tail.pos += 1.0;
                tail.velocity *= 0.85f;
                if (tail.pos < cell.segLen - 0.25)
                    ev.push_back (tail);
            }
            break;
        case Transform::OmitLast:
            if (! ev.empty())
                ev.pop_back();
            break;
        case Transform::DensifyEnd:
            if (! ev.empty())
            {
                Event echo = ev.back();
                echo.pos += 0.5;
                echo.velocity = juce::jlimit (0.05f, 1.0f, echo.velocity * 0.8f);
                echo.gateSteps = 0.5;
                if (echo.pos < cell.segLen - 0.25)
                    ev.push_back (echo);
                if (rng.nextFloat() < 0.5f && echo.pos + 0.5 < cell.segLen - 0.25)
                {
                    Event echo2 = echo;
                    echo2.pos += 0.5;
                    echo2.velocity *= 0.85f;
                    ev.push_back (echo2);
                }
            }
            else
                rng.nextFloat();   // keep the stream aligned
            break;
        case Transform::EchoSofter:
            for (auto& e : ev)
                e.velocity = juce::jlimit (0.05f, 1.0f, e.velocity * 0.65f);
            break;
        case Transform::RoleSubstitute:
            for (auto& e : ev)
                if (e.role != Role::LOW && rng.nextFloat() < 0.7f)
                    e.role = e.role == Role::HIGH ? Role::MID : Role::HIGH;
            break;
        case Transform::Displace:
            for (auto& e : ev) e.pos += 1.0;
            ev.erase (std::remove_if (ev.begin(), ev.end(),
                [&] (const Event& e) { return e.pos >= cell.segLen - 0.25; }), ev.end());
            break;
        case Transform::QuestionSilence:
            // The ending is withheld: nothing may sound in the last quarter.
            ev.erase (std::remove_if (ev.begin(), ev.end(),
                [&] (const Event& e) { return e.pos >= cell.segLen * 0.75; }), ev.end());
            break;
        case Transform::AnswerLowResolve:
        {
            for (auto& e : ev)
                e.velocity = juce::jlimit (0.05f, 1.0f, e.velocity * 0.7f);
            Event low;
            low.pos = cell.segLen - 4.0 >= 0.0 ? cell.segLen - 4.0 : 0.0;
            low.role = Role::LOW;
            low.velocity = 0.85f;
            low.gateSteps = 2.0;
            ev.push_back (low);
            break;
        }
    }
    sortCell();
    return out;
}

float Motif::similarity (const Cell& a, const Cell& b)
{
    // Meter-aware weighted overlap on a 32nd grid: beats dominate, weak
    // subdivisions barely count. Role agreement earns full credit, a hit in
    // the same slot with a different role earns half.
    auto weightOf = [] (double pos)
    {
        const double frac = pos - std::floor (pos);
        const int step = (int) std::floor (pos);
        if (frac > 0.01 && frac < 0.99) return 0.2f;   // off-grid
        if (step % 4 == 0) return 1.0f;                // beat
        if (step % 2 == 0) return 0.6f;                // 8th
        return 0.35f;                                  // 16th
    };
    auto bucket = [] (double pos) { return juce::roundToInt (pos * 2.0); };

    float overlap = 0.0f, total = 0.0f;
    std::vector<bool> used (b.events.size(), false);
    for (const auto& ea : a.events)
    {
        const float w = weightOf (ea.pos);
        total += w;
        for (size_t j = 0; j < b.events.size(); ++j)
        {
            if (used[j] || bucket (b.events[j].pos) != bucket (ea.pos))
                continue;
            used[j] = true;
            overlap += ea.role == b.events[j].role ? w : w * 0.5f;
            break;
        }
    }
    for (size_t j = 0; j < b.events.size(); ++j)
        if (! used[j])
            total += weightOf (b.events[j].pos);
    return total > 0.0f ? overlap / total : 1.0f;
}

Motif::Cell Motif::extract (const Pattern& p, int segment, double segLen)
{
    Cell c;
    c.segLen = segLen;
    const double from = segment * segLen, to = from + segLen;
    for (const auto& e : p.events)
    {
        if (e.pos < from || e.pos >= to || e.protectedAnchor || e.roll)
            continue;
        // Ghosts (the quiet 0.5-gate ticks) are texture, not motif.
        if (e.velocity <= 0.28f && e.gateSteps == 0.5)
            continue;
        Event n = e;
        n.pos -= from;
        c.events.push_back (n);
    }
    return c;
}

Motif::CallResponsePlan Motif::callResponseOf (const PhrasePlan& plan, Family family,
                                               float randomness, juce::uint64 motifSeed)
{
    CallResponsePlan cr;
    for (int seg = 0; seg + 1 < plan.segments(); ++seg)
        if (plan.roles[(size_t) seg] == PhraseRole::Call
            && plan.roles[(size_t) seg + 1] == PhraseRole::Response)
        {
            cr.callSegment = seg;
            cr.responseSegment = seg + 1;
            juce::Random rng ((juce::int64) (motifSeed ^ 0xCA11ull));
            cr.relation = choose (PhraseRole::Response, family, randomness, rng);
            cr.targetSimilarity = juce::jlimit (0.25f, 0.9f, 0.75f - 0.35f * randomness);
            break;
        }
    return cr;
}

} // namespace orcha
