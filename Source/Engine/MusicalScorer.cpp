#include "MusicalScorer.h"
#include "RhythmStyle.h"
#include "SectionProfile.h"
#include "SilencePlanner.h"
#include <cmath>

namespace orcha
{

MusicalScorer::Features MusicalScorer::extract (const Pattern& p)
{
    Features f;
    const int steps = p.stepCount();
    float total = 0.0f, offbeat = 0.0f;
    std::array<float, 3> roleW {};
    std::vector<bool> occupied ((size_t) steps, false);

    for (const auto& e : p.events)
    {
        // Squared velocity: what you HEAR carries the weight. A quiet ghost
        // barely registers, exactly as it barely registers to the ear.
        const float w = e.velocity * e.velocity;
        total += w;
        f.onsetHist[(size_t) (juce::jlimit (0, 15,
            juce::roundToInt (std::floor (e.pos)) % 16))] += w;
        if (std::fmod (e.pos, 4.0) >= 0.01)
            offbeat += w;
        if (e.role == Role::LOW) roleW[0] += w;
        else if (e.role == Role::MID) roleW[1] += w;
        else if (e.role == Role::HIGH) roleW[2] += w;
        occupied[(size_t) juce::jlimit (0, steps - 1, (int) e.pos)] = true;
        f.hasFill = f.hasFill || e.roll;
    }

    if (total > 0.0f)
    {
        for (auto& h : f.onsetHist) h /= total;
        for (auto& r : roleW) r /= total;
        f.syncopation = offbeat / total;
    }
    f.roleBalance = roleW;
    f.density = (float) p.events.size() / (float) steps;
    f.silenceRatio = 1.0f - (float) std::count (occupied.begin(), occupied.end(), true)
                          / (float) steps;
    f.lead = roleW[1] >= roleW[2] ? Role::MID : Role::HIGH;
    const auto tension = TensionModel::measure (p);
    for (size_t i = 0; i < 4 && i < tension.size(); ++i)
        f.tension[i] = tension[i];
    return f;
}

float MusicalScorer::distance (const Features& a, const Features& b)
{
    // Meter-aware onset difference: beats matter, weak 16ths matter less.
    float onset = 0.0f;
    for (int i = 0; i < 16; ++i)
    {
        const float w = i % 4 == 0 ? 1.0f : i % 2 == 0 ? 0.6f : 0.4f;
        onset += w * std::abs (a.onsetHist[(size_t) i] - b.onsetHist[(size_t) i]);
    }
    float tensionDiff = 0.0f;
    for (int i = 0; i < 4; ++i)
        tensionDiff += std::abs (a.tension[(size_t) i] - b.tension[(size_t) i]);
    float roleDiff = 0.0f;
    for (int i = 0; i < 3; ++i)
        roleDiff += std::abs (a.roleBalance[(size_t) i] - b.roleBalance[(size_t) i]);

    return juce::jlimit (0.0f, 1.5f,
        onset * 1.2f
        + std::abs (a.syncopation - b.syncopation) * 0.5f
        + std::abs (a.density - b.density) * 0.3f
        + std::abs (a.silenceRatio - b.silenceRatio) * 0.4f
        + (a.lead != b.lead ? 0.12f : 0.0f)
        + tensionDiff * 0.15f
        + roleDiff * 0.3f);
}

juce::String MusicalScorer::ScoreBreakdown::describe() const
{
    juce::String s;
    s << "pulse=" << juce::String (pulseClarity, 2)
      << " sync=" << juce::String (syncopationFit, 2)
      << " space=" << juce::String (negativeSpace, 2)
      << " dens=" << juce::String (densityFit, 2)
      << " anchor=" << juce::String (anchorIntegrity, 2)
      << " bound=" << juce::String (boundaryQuality, 2)
      << " pen=" << juce::String (penalties, 2)
      << " total=" << juce::String (total, 2);
    return s;
}

MusicalScorer::ScoreBreakdown MusicalScorer::score (const Pattern& p,
                                                    const GeneratorSettings& s)
{
    ScoreBreakdown b;
    const auto& style = RhythmStyle::get (s.family);
    const auto profile = sectionProfile (s.mode);
    const auto f = extract (p);
    const int steps = p.stepCount();

    // Pulse clarity: the section's expected beat coverage, met - not maxed.
    float beatsCovered = 0.0f;
    int beats = 0;
    for (int step = 0; step < steps; step += 4)
    {
        ++beats;
        for (const auto& e : p.events)
            if (std::abs (e.pos - step) < 0.26)
            {
                beatsCovered += 1.0f;
                break;
            }
    }
    const float coverage = beats > 0 ? beatsCovered / (float) beats : 0.0f;
    const float expectedCoverage = s.mode == Mode::BREAK ? 0.45f
                                 : s.mode == Mode::BUILD ? 0.7f : 0.85f;
    b.pulseClarity = 1.0f - juce::jmin (1.0f,
        std::abs (coverage - expectedCoverage) * 1.5f);
    const bool hasDownbeat = std::any_of (p.events.begin(), p.events.end(),
        [] (const Event& e) { return e.pos < 0.26 && e.role == Role::LOW; });
    if (! hasDownbeat && s.mode != Mode::BREAK)
        b.pulseClarity *= 0.4f;

    // Syncopation inside the style's target range.
    const float lo = style.syncopationTargetLo, hi = style.syncopationTargetHi;
    b.syncopationFit = f.syncopation < lo ? 1.0f - (lo - f.syncopation) * 2.0f
                     : f.syncopation > hi ? 1.0f - (f.syncopation - hi) * 2.0f
                     : 1.0f;
    b.syncopationFit = juce::jmax (0.0f, b.syncopationFit);

    // Negative space: the section's expected air.
    const float expectedSilence = s.mode == Mode::BREAK ? 0.5f
                                : s.family == Family::CINEMATIC ? 0.55f
                                : s.mode == Mode::DROP ? 0.25f : 0.3f;
    b.negativeSpace = juce::jmax (0.0f,
        1.0f - std::abs (f.silenceRatio - expectedSilence) * 1.6f);

    // Density near what the macro asked for.
    const float expectedDensity = (0.45f + 0.9f * s.density) * profile.baseDensity * 2.0f;
    b.densityFit = juce::jmax (0.0f,
        1.0f - std::abs (f.density - expectedDensity) * 1.2f);

    // Anchors: the low pulse the style promises.
    float lowBeats = 0.0f;
    for (int step = 0; step < steps; step += 4)
        for (const auto& e : p.events)
            if (e.role == Role::LOW && std::abs (e.pos - step) < 0.26)
            {
                lowBeats += 1.0f;
                break;
            }
    const float expectedLow = style.fourFloorAnchor && s.mode != Mode::BREAK
                            ? 0.9f : 0.5f;
    b.anchorIntegrity = 1.0f - juce::jmin (1.0f,
        std::abs (lowBeats / juce::jmax (1, beats) - expectedLow));

    // Boundary: the loop must state its start; a stray quiet tail orphan is
    // a weak return.
    b.boundaryQuality = 1.0f;
    const bool startsWithin = std::any_of (p.events.begin(), p.events.end(),
        [] (const Event& e) { return e.pos < 1.0; });
    if (! startsWithin)
        b.boundaryQuality -= 0.5f;
    for (const auto& e : p.events)
        if (e.pos > steps - 1 && ! e.roll && ! e.protectedAnchor
            && e.velocity < 0.4f)
        {
            b.boundaryQuality -= 0.25f;
            break;
        }
    b.boundaryQuality = juce::jmax (0.0f, b.boundaryQuality);

    // Penalties: dead repetition and functionless loners.
    float penalties = 0.0f;
    int identicalRun = 0;
    for (int step = 0; step + 1 < steps; ++step)
    {
        auto stepMask = [&] (int st)
        {
            int mask = 0;
            for (const auto& e : p.events)
                if (std::abs (e.pos - st) < 0.26)
                    mask |= 1 << (int) e.role;
            return mask;
        };
        if (stepMask (step) != 0 && stepMask (step) == stepMask (step + 1))
            ++identicalRun;
        else
            identicalRun = 0;
        if (identicalRun >= 5)
        {
            penalties += 0.2f;
            break;
        }
    }
    if (s.family != Family::CINEMATIC)
        for (const auto& e : p.events)
        {
            bool lonely = true;
            for (const auto& o : p.events)
                if (&o != &e && std::abs (o.pos - e.pos) <= 4.0)
                    lonely = false;
            if (lonely)
            {
                penalties += 0.15f;
                break;
            }
        }
    b.penalties = penalties;

    b.total = b.pulseClarity * 0.24f + b.syncopationFit * 0.16f
            + b.negativeSpace * 0.16f + b.densityFit * 0.14f
            + b.anchorIntegrity * 0.18f + b.boundaryQuality * 0.12f
            - penalties;
    return b;
}

std::vector<int> MusicalScorer::selectDiverse (const std::vector<Pattern>& pool,
                                               const std::vector<Features>& features,
                                               const std::vector<ScoreBreakdown>& scores,
                                               int count)
{
    std::vector<int> selected;
    if (pool.empty())
        return selected;

    // A LOW floor relative to the pool's best: quality gates out the broken,
    // diversity decides among the living. A high floor would flatten the 12
    // cards into the scorer's single taste.
    float best = -1.0f;
    for (const auto& sc : scores)
        best = juce::jmax (best, sc.total);
    const float floorQ = best * 0.55f;

    auto personaAffinity = [&] (int slot, const Features& f)
    {
        switch ((slot / 3) % 4)
        {
            case 0:  return 0.5f * (1.0f - f.syncopation)        // direct
                          + 0.3f * f.roleBalance[0];
            case 1:  return 0.5f * f.density                     // driving
                          - 0.2f * f.silenceRatio;
            case 2:  return 0.7f * f.syncopation;                // organic
            default: return 0.6f * f.silenceRatio                // dramatic
                          + (f.hasFill ? 0.15f : 0.0f);
        }
    };

    std::vector<bool> used (pool.size(), false);
    for (int slot = 0; slot < count; ++slot)
    {
        int bestIdx = -1;
        float bestScore = -1.0e9f;
        for (int pass = 0; pass < 2 && bestIdx < 0; ++pass)
            for (size_t j = 0; j < pool.size(); ++j)
            {
                if (used[j])
                    continue;
                if (pass == 0 && scores[j].total < floorQ)
                    continue;   // second pass ignores the floor if starved
                float minDist = 10.0f;
                for (int sIdx : selected)
                    minDist = juce::jmin (minDist,
                        distance (features[j], features[(size_t) sIdx]));
                if (selected.empty())
                    minDist = 0.5f;
                const float sel = scores[j].total
                                + 2.0f * minDist
                                + personaAffinity (slot, features[j]);
                if (sel > bestScore + 1.0e-6f)
                {
                    bestScore = sel;
                    bestIdx = (int) j;
                }
            }
        if (bestIdx < 0)
            break;
        used[(size_t) bestIdx] = true;
        selected.push_back (bestIdx);
    }
    return selected;
}

} // namespace orcha
