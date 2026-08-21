#include "LoopGenerator.h"
#include "PatternValidator.h"
#include "PhrasePlanner.h"
#include "FeelVector.h"
#include "Motif.h"
#include "SilencePlanner.h"
#include "FillBank.h"
#include <algorithm>

namespace orcha
{

juce::uint64 LoopGenerator::deriveSeed (juce::uint64 master, int optionIndex)
{
    juce::uint64 z = master + 0x9E3779B97F4A7C15ULL * (juce::uint64) (optionIndex + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

namespace
{
    // juce::Random is a 48-bit LCG; seeded per pattern it is deterministic and
    // identical on every platform, which the state format depends on.
    struct Rng
    {
        explicit Rng (juce::uint64 seed) : r ((juce::int64) seed) {}
        float uni()               { return r.nextFloat(); }
        bool  chance (float p)    { return r.nextFloat() < p; }
        int   pick (int n)        { return r.nextInt (juce::jmax (1, n)); }
        juce::Random r;
    };

    bool hasEventAt (const std::vector<Event>& events, double pos, Role role)
    {
        for (const auto& e : events)
            if (e.role == role && std::abs (e.pos - pos) < 0.26)
                return true;
        return false;
    }

    bool stepOccupied (const std::vector<Event>& events, double pos)
    {
        for (const auto& e : events)
            if (std::abs (e.pos - pos) < 0.26)
                return true;
        return false;
    }
}

Pattern LoopGenerator::generate (juce::uint64 seed, const GeneratorSettings& settings)
{
    return generate (seed, deriveSeed (seed, 4242), settings);
}

Pattern LoopGenerator::generate (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                                 const GeneratorSettings& settings)
{
    Pattern p;
    p.seed = motifSeed;
    p.ornamentSeed = ornamentSeed;
    p.settings = settings;

    // Two independent streams: rngM decides the motif, rng decides the
    // decoration. rngM's draw order must stay fixed - a new draw inserted
    // before an old one changes every stored pattern.
    Rng rngM (motifSeed);
    Rng rng (ornamentSeed);
    const auto& style = RhythmStyle::get (settings.family);
    const auto profile = sectionProfile (settings.mode);
    const int steps = p.stepCount();
    const int bars = settings.bars;

    // --- 1. skeleton + phrase plan (motif) -------------------------------------
    const auto& skel = style.skeletons[(size_t) rngM.pick ((int) style.skeletons.size())];
    p.swing = skel.defaultSwing * (0.6 + 0.8 * rngM.uni());
    if (settings.mode == Mode::GROOVE)
        p.swing = juce::jlimit (0.0, 1.0, p.swing + 0.08 * rngM.uni());

    // Which role leads (gets the ornament budget) varies per option.
    const Role leadRole = rngM.chance (0.5f) ? Role::HIGH : Role::MID;

    // --- 2. protected anchors + motif across all bars (motif) ------------------
    // Bar-level mutation mask: later bars may vary, bar 0 states the motif.
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool variedBar = bar > 0 && rngM.chance (0.35f + 0.4f * settings.randomness);
        for (const auto& hit : skel.hits)
        {
            // Sparse sections thin even the skeleton - but never the downbeat.
            const bool downbeat = hit.step == 0 && hit.role == Role::LOW;
            if (profile.sparse && ! downbeat && rngM.chance (0.45f))
                continue;
            if (variedBar && ! downbeat && hit.role != Role::LOW && rngM.chance (0.3f))
                continue;

            Event e;
            e.pos = bar * 16 + hit.step;
            e.role = hit.role;
            e.velocity = hit.accent;
            e.protectedAnchor = hit.role == Role::LOW || hit.accent >= 0.75f;
            // Quiet skeleton LOWs are rolling-bass ghosts, not kicks that
            // ring: choke them so the pump stays tight. One step, so even
            // psytrance 16th bass lines stay clean of each other.
            if (hit.role == Role::LOW && hit.accent < 0.5f)
                e.gateSteps = 1.0;
            p.events.push_back (e);
        }
    }

    // --- 3/7. density: ornaments in, weak events out (macro-controlled) -------
    const float densityScale = 0.35f + 1.3f * settings.density;
    const int candidateCount = (int) skel.ornamentSteps.size() * bars;
    int targetAdds = juce::roundToInt ((float) candidateCount
                                       * profile.baseDensity * style.ornamentDensity
                                       * densityScale);

    std::vector<int> candidates;
    for (int bar = 0; bar < bars; ++bar)
        for (int os : skel.ornamentSteps)
            candidates.push_back (bar * 16 + os);
    // Deterministic shuffle.
    for (int i = (int) candidates.size() - 1; i > 0; --i)
        std::swap (candidates[(size_t) i], candidates[(size_t) rng.pick (i + 1)]);

    for (int cand : candidates)
    {
        if (targetAdds <= 0)
            break;
        const double pos = (double) cand;
        // AFRO interlocks: ornaments live where anchors do not.
        if (style.interlocking && stepOccupied (p.events, pos))
            continue;
        // BUILD ramps density: early positions are less likely to fill.
        if (profile.densityRamp && ! rng.chance (0.25f + 0.75f * (float) (pos / steps)))
            continue;

        Event e;
        e.pos = pos;
        e.role = rng.chance (0.65f) ? leadRole : (leadRole == Role::HIGH ? Role::MID : Role::HIGH);
        if (hasEventAt (p.events, pos, e.role))
            continue;
        e.velocity = profile.velocityFloor + 0.25f * rng.uni();
        e.gateSteps = 0.75;   // ornaments choke so busy passages stay readable
        p.events.push_back (e);
        --targetAdds;
    }

    // Low macro density also prunes non-protected skeleton hits.
    if (settings.density < 0.35f)
    {
        const float dropChance = (0.35f - settings.density) * 2.2f;
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
            [&] (const Event& e)
            { return ! e.protectedAnchor && rng.chance (dropChance); }),
            p.events.end());
    }

    // Designed ghost layer: very quiet lead-role ticks in the empty off-grid
    // slots. This is the pocket between the written hits - deliberate, not
    // leftover randomness. Sparse sections keep their silence instead.
    if (! profile.sparse)
    {
        const float ghostP = style.ghostiness * (0.18f + 0.45f * settings.density);
        for (int step = 0; step < steps; ++step)
        {
            if (step % 4 == 0)
                continue;                       // beats belong to the anchors
            const double pos = (double) step;
            if (stepOccupied (p.events, pos) || ! rng.chance (ghostP))
                continue;
            Event g;
            g.pos = pos;
            g.role = leadRole;
            g.velocity = 0.10f + 0.15f * rng.uni();
            g.gateSteps = 0.5;
            p.events.push_back (g);
        }
    }

    // --- 4. randomness: distance from the skeleton ----------------------------
    const float r = settings.randomness;
    for (auto& e : p.events)
    {
        if (e.protectedAnchor)
            continue;
        if (rng.chance (r * 0.4f))
            e.pos = juce::jlimit (0.0, (double) steps - 1.0, e.pos + (rng.chance (0.5f) ? 1.0 : -1.0));
        if (rng.chance (r * 0.25f) && e.role != Role::LOW)
            e.role = e.role == Role::HIGH ? Role::MID : Role::HIGH;
        if (rng.chance (r * 0.15f))
            e.pitchSemis = rng.chance (0.5f) ? 2 : -2;
    }
    // Grace "ka" ornaments in front of accented off-hits.
    if (r > 0.15f)
    {
        std::vector<Event> graces;
        for (const auto& e : p.events)
            if (e.role == Role::HIGH && ! e.protectedAnchor && e.pos >= 1.0
                && rng.chance (r * 0.3f))
            {
                Event g = e;
                g.pos -= 0.5;
                g.velocity *= 0.5f;
                g.gateSteps = 0.5;
                graces.push_back (g);
            }
        p.events.insert (p.events.end(), graces.begin(), graces.end());
    }

    // --- 6. question & answer between phrase halves ---------------------------
    if (rng.chance (0.6f))
    {
        const double half = steps / 2.0;
        for (auto& e : p.events)
            if (e.pos >= half && ! e.protectedAnchor)
            {
                if (rng.chance (0.25f))
                    e.velocity = juce::jlimit (0.05f, 1.0f, e.velocity + (rng.chance (0.5f) ? 0.2f : -0.15f));
                if (rng.chance (0.15f))
                    e.pos = juce::jmin ((double) steps - 1.0, e.pos + 0.5); // delayed answer
            }
    }

    // --- 8. energy + groove: velocity contour, accent map, feel ----------------
    const float energy = settings.energy;
    for (auto& e : p.events)
    {
        const bool onBeat = std::fmod (e.pos, 4.0) < 0.01;
        float v = e.velocity;
        if (onBeat)
            v *= 0.85f + 0.35f * energy;                       // accent lift
        else
            v *= 1.0f - profile.velocityContrast * 0.35f * (1.0f - energy);

        // The family's cyclic accent map is the pocket: every 16th position
        // has its own weight. Anchors keep most of their written accent;
        // everything else takes the map in full.
        const int mapStep = juce::jlimit (0, 15,
            juce::roundToInt (std::floor (e.pos)) % 16);
        const float mapW = style.accentMap[(size_t) mapStep];
        v *= e.protectedAnchor ? 0.6f + 0.4f * mapW : mapW;

        if (profile.densityRamp)                               // BUILD contour
            v *= 0.55f + 0.45f * (float) (e.pos / steps);
        e.velocity = juce::jlimit (0.05f, 1.0f, v);

        // Weight at high energy: occasionally drop the LOW pitch for impact.
        if (e.role == Role::LOW && energy > 0.7f && rng.chance (0.2f))
            e.pitchSemis = -1;
        // Humanize timing, but never the structural anchors. On top of the
        // jitter, each role takes the family's feel: hats laid back, or the
        // mid layer pushing - constant, so it reads as feel, not sloppiness.
        if (! e.protectedAnchor)
        {
            e.microMs = (rng.uni() * 2.0f - 1.0f) * 3.0f * r;
            if (e.role == Role::HIGH)
                e.microMs += style.highFeelMs;
            else if (e.role == Role::MID)
                e.microMs += style.midFeelMs;
        }
    }

    // DROP breathing: silence right before the next downbeat, so the loop
    // "inhales" - a hole at the end of the phrase, not random gaps.
    if (settings.mode == Mode::DROP && rng.chance (profile.silenceChance + 0.3f * energy))
    {
        const double holeStart = steps - (rng.chance (0.5f) ? 1.0 : 2.0);
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
            [&] (const Event& e) { return e.pos >= holeStart && ! (e.pos == 0.0); }),
            p.events.end());
    }

    // --- 9. fills and rolls at the phrase end ---------------------------------
    const float fillP = juce::jlimit (0.0f, 0.95f,
        profile.fillChance + (settings.mode == Mode::DROP ? 0.25f * energy : 0.0f));

    // A BUILD can end two ways: rolling into the drop, or holding its breath
    // ("1, 2... BOOM"). The choice is made first so the two never fight.
    const bool gapEnding = settings.mode == Mode::BUILD && rng.chance (0.4f);

    if (! gapEnding && rng.chance (fillP))
    {
        const Role rollRole = rng.chance (0.4f) ? Role::MID : Role::HIGH;
        const int rollSteps = 2 + rng.pick (settings.mode == Mode::BUILD ? 3 : 2);
        const double rollStart = steps - rollSteps;
        // Clear the runway so the roll reads as one gesture.
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
            [&] (const Event& e)
            { return e.pos >= rollStart && e.role == rollRole && ! e.protectedAnchor; }),
            p.events.end());

        // BUILD accelerates for real: the spacing shrinks hit by hit, 16ths
        // tightening into 32nds - tension you can hear, not a straight grid.
        // The other modes choose between straight 32nds and a triplet feel.
        const bool accelerate = settings.mode == Mode::BUILD;
        double spacing = accelerate ? 0.9
                       : rng.chance (0.3f) ? 1.0 / 3.0 : 0.5;

        double pos = rollStart;
        while (pos < steps - 0.25)
        {
            Event e;
            e.pos = pos;
            e.role = rollRole;
            e.roll = true;
            const float t = (float) (pos - rollStart) / (float) rollSteps;
            e.velocity = juce::jlimit (0.1f, 1.0f, 0.35f + 0.6f * t * (0.5f + 0.5f * energy));
            e.gateSteps = spacing;
            if (settings.mode == Mode::BUILD)
                e.pitchSemis = (int) (t * (3.0f + 5.0f * energy));   // rising tension
            p.events.push_back (e);

            pos += spacing;
            if (accelerate)
                spacing = juce::jmax (0.25, spacing * 0.8);
        }
    }

    // --- transition drama -------------------------------------------------------
    if (settings.mode == Mode::BUILD)
    {
        // The countdown: the last bar's LOW anchors become a pitch walk -
        // four predictable kicks stepping down (or climbing) into the next
        // downbeat. The drop everyone hears coming, on purpose.
        if (rng.chance (0.65f))
        {
            const bool descending = rng.chance (0.6f);
            std::vector<Event*> countdown;
            for (auto& e : p.events)
                if (e.role == Role::LOW && e.protectedAnchor
                    && e.pos >= steps - 16 && std::fmod (e.pos, 4.0) < 0.01)
                    countdown.push_back (&e);
            const int n = (int) countdown.size();
            for (int k = 0; k < n; ++k)
            {
                const float t = n > 1 ? (float) k / (float) (n - 1) : 1.0f;
                const int walk = juce::roundToInt (t * (5.0f + 4.0f * energy));
                countdown[(size_t) k]->pitchSemis += descending ? -walk : walk;
                countdown[(size_t) k]->velocity =
                    juce::jlimit (0.05f, 1.0f, 0.8f + 0.2f * t);
            }
        }

        // "1, 2... BOOM": the breath ending - the tail empties, one accented
        // pickup throws into the entrance.
        if (gapEnding)
        {
            const double holeStart = steps - 2.0;
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [holeStart] (const Event& e) { return e.pos >= holeStart; }),
                p.events.end());
            if (rng.chance (0.7f))
            {
                Event pickup;
                pickup.pos = steps - 1.0;
                pickup.role = Role::LOW;
                pickup.velocity = 1.0f;
                pickup.pitchSemis = rng.chance (0.4f) ? -2 : 0;
                pickup.protectedAnchor = true;
                p.events.push_back (pickup);
            }
        }
    }

    if (settings.mode == Mode::BREAK)
    {
        // A reversed swell in the last beat, sucked into the loop point -
        // the tail of the brightest sample played backwards swells INTO the
        // next downbeat.
        if (rng.chance (0.45f))
        {
            Event swell;
            swell.pos = steps - 3.75;   // its own 64th, clear of any grid hat
            swell.role = Role::HIGH;
            swell.reverse = true;
            swell.gateSteps = 3.45;     // ends just before the boundary fade
            swell.velocity = 0.5f + 0.25f * energy;
            p.events.push_back (swell);
        }
        // One lone deep hit in the emptiness - dread, pitched down.
        if (rng.chance (0.35f))
        {
            const double pos = 4.0 + rng.pick (juce::jmax (1, steps - 8));
            if (! stepOccupied (p.events, pos))
            {
                Event deep;
                deep.pos = pos;
                deep.role = Role::LOW;
                deep.velocity = 0.85f;
                deep.pitchSemis = -5;
                deep.gateSteps = 3.0;
                p.events.push_back (deep);
            }
        }
    }

    // 4-bar phrases get sentence structure: a lighter answer-fill at the half
    // (end of bar 2), so the phrase reads 2+2 instead of 4x1.
    if (bars == 4 && rng.chance (0.25f + profile.fillChance * 0.4f))
    {
        const Role halfRole = rng.chance (0.5f) ? Role::MID : Role::HIGH;
        for (double pos = 30.5; pos < 31.9; pos += 0.5)
        {
            Event e;
            e.pos = pos;
            e.role = halfRole;
            e.roll = true;
            e.velocity = 0.3f + 0.2f * rng.uni();
            e.gateSteps = 0.5;
            p.events.push_back (e);
        }
    }

    // --- 10. validation happens in PatternValidator (caller runs it) ----------
    std::sort (p.events.begin(), p.events.end(), eventBefore);
    return p;
}

// =============================================================================
// ENGINE 2: the same musical toolbox, driven by a phrase plan. Every segment
// (bar, or beat in a 1-bar loop) has a functional role decided before any
// event lands, and the ornament/ghost/velocity stages follow deterministic
// feel trajectories instead of flat constants.
// =============================================================================
Pattern LoopGenerator::generateV2 (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                                   const GeneratorSettings& settings)
{
    return generateV2 (motifSeed, ornamentSeed, settings, TraitsByRole {});
}

Pattern LoopGenerator::generateV2 (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                                   const GeneratorSettings& settings,
                                   const TraitsByRole& traits,
                                   Destination destination)
{
    // FILL is its own vocabulary: one authored bar from the curated bank,
    // not a phrase-planned loop. Destinations do not apply - a fill IS the
    // transition.
    if (settings.mode == Mode::FILL)
        return FillBank::build (motifSeed, ornamentSeed, settings, traits);

    Pattern p;
    p.seed = motifSeed;
    p.ornamentSeed = ornamentSeed;
    p.settings = settings;
    p.algo = 2;
    p.destination = destination;

    Rng rngM (motifSeed);
    Rng rng (ornamentSeed);
    const auto& style = RhythmStyle::get (settings.family);
    const auto profile = sectionProfile (settings.mode);
    const int steps = p.stepCount();
    const int bars = settings.bars;

    // The plan and the feel: identity-level, so they derive from the motif
    // seed and survive "same groove, another take".
    const auto plan = PhrasePlanner::plan (settings.mode, bars, motifSeed);
    const auto feel = FeelVector::derive (settings, motifSeed);
    const auto traj = FeelTrajectory::derive (settings.mode, bars, feel);
    // Silence is planned before decoration exists, and protected after.
    const auto silence = SilencePlanner::plan (settings.mode, settings.family,
                                               bars, plan, feel, motifSeed);
    auto segOf = [&] (double pos) { return plan.segmentOf (pos, steps); };
    auto segRole = [&] (int seg)
    {
        return plan.roles[(size_t) juce::jlimit (0, plan.segments() - 1, seg)];
    };
    // Role interaction: how many distinct voices may share an OFF-beat slot
    // (beats can stack freely - that is reinforcement), and whether a ghost
    // needs a neighbor to hold on to (the arabic "ka" grammar).
    auto rolesAt = [&] (double pos)
    {
        int mask = 0;
        for (const auto& e : p.events)
            if (std::abs (e.pos - pos) < 0.26)
                mask |= 1 << (int) e.role;
        int n = 0;
        for (int b = 0; b < 5; ++b)
            n += (mask >> b) & 1;
        return n;
    };
    auto offbeatStackFull = [&] (double pos)
    {
        const bool onBeat = std::fmod (pos, 4.0) < 0.01;
        return ! onBeat && rolesAt (pos) >= style.maxOffbeatStack;
    };
    auto hasNeighbor = [&] (double pos)
    {
        for (const auto& e : p.events)
            if (std::abs (e.pos - pos) <= 1.01 && std::abs (e.pos - pos) > 0.01)
                return true;
        return false;
    };

    // How much decoration a segment's function invites, on top of the
    // density trajectory: the Accelerate bar earns its extra weight here.
    auto roleBudget = [] (PhraseRole role)
    {
        switch (role)
        {
            case PhraseRole::Accelerate:
            case PhraseRole::Lift:       return 1.35f;
            case PhraseRole::Impact:
            case PhraseRole::Establish:  return 0.85f;
            case PhraseRole::Develop:    return 1.1f;
            case PhraseRole::Response:   return 1.05f;
            default:                     return 1.0f;
        }
    };

    // --- 1. skeleton + swing + lead role (motif stream, same as v1) ------------
    const auto& skel = style.skeletons[(size_t) rngM.pick ((int) style.skeletons.size())];
    // The groove's NAME rides along: the worker appends it to the card label,
    // so "GROOVE 04" becomes "GROOVE 04 - GARAGE SHUFFLE".
    p.name = juce::String (skel.name).replaceCharacter ('_', ' ').toUpperCase();
    p.swing = skel.defaultSwing * (0.6 + 0.8 * rngM.uni());
    if (settings.mode == Mode::GROOVE)
        p.swing = juce::jlimit (0.0, 1.0, p.swing + 0.08 * rngM.uni());
    const Role leadRole = rngM.chance (0.5f) ? Role::HIGH : Role::MID;

    // --- 2. anchors, governed by each segment's phrase role --------------------
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const auto& hit : skel.hits)
        {
            const double pos = bar * 16 + hit.step;
            const auto role = segRole (segOf (pos));
            const bool downbeat = hit.step == 0 && hit.role == Role::LOW;

            if (profile.sparse && ! downbeat && rngM.chance (0.45f))
                continue;

            Role eventRole = hit.role;
            switch (role)
            {
                case PhraseRole::Develop:
                case PhraseRole::Lift:
                    if (! downbeat && hit.role != Role::LOW && rngM.chance (0.25f))
                        continue;
                    break;
                case PhraseRole::Contrast:
                    // The B section: thinner, and the mid/high voices trade.
                    if (! downbeat && rngM.chance (0.3f))
                        continue;
                    if (eventRole != Role::LOW && rngM.chance (0.35f))
                        eventRole = eventRole == Role::HIGH ? Role::MID : Role::HIGH;
                    break;
                case PhraseRole::Breath:
                    if (! downbeat && rngM.chance (0.75f))
                        continue;
                    break;
                case PhraseRole::Vacuum:
                    if (! downbeat)
                        continue;
                    break;
                default:
                    break;   // Impact/Establish/Lock/Call/Response/... keep all
            }

            Event e;
            e.pos = pos;
            e.role = eventRole;
            e.velocity = hit.accent;
            e.protectedAnchor = hit.role == Role::LOW || hit.accent >= 0.75f;
            if (hit.role == Role::LOW && hit.accent < 0.5f)
                e.gateSteps = 1.0;
            p.events.push_back (e);
        }
    }

    // --- 3. THE MOTIF: one decorated cell, developed across the phrase --------
    // The cell is built from the MOTIF stream, so "same groove, another take"
    // now preserves the decoration identity too - not only the anchors.
    const double motifSegLen = bars > 1 ? 16.0 : 8.0;
    const int motifSegments = bars > 1 ? plan.segments() : 2;
    auto motifRole = [&] (int mseg)
    {
        // 1-bar loops collapse the beat-level plan into two half-bar cells.
        return bars > 1 ? segRole (mseg)
                        : plan.roles[(size_t) (mseg == 0 ? 0 : 2)];
    };

    Motif::Cell cell;
    cell.segLen = motifSegLen;
    {
        const float densityScale = 0.25f + 1.6f * settings.density;
        const float fillP = juce::jlimit (0.1f, 0.95f,
            profile.baseDensity * style.ornamentDensity * densityScale * 1.4f);
        for (int os : skel.ornamentSteps)
        {
            if ((double) os >= motifSegLen)
                continue;
            if (! rngM.chance (fillP))
                continue;
            Event e;
            e.pos = os;
            e.role = rngM.chance (0.65f) ? leadRole
                    : (leadRole == Role::HIGH ? Role::MID : Role::HIGH);
            e.velocity = profile.velocityFloor + 0.25f * rngM.uni();
            e.gateSteps = 0.75;
            cell.events.push_back (e);
        }
    }

    // Each segment restates the cell through its phrase role's transformation
    // - explicit development, not random mutation of a copy.
    juce::Random motifRng ((juce::int64) (motifSeed ^ 0x7A57E5ull));
    for (int mseg = 0; mseg < motifSegments; ++mseg)
    {
        const auto role = motifRole (mseg);
        if (role == PhraseRole::Breath || role == PhraseRole::Vacuum)
            continue;
        const auto tr = mseg == 0 ? Motif::Transform::ExactRepeat
                                  : Motif::choose (role, settings.family,
                                                   settings.randomness, motifRng);
        auto placed = Motif::apply (cell, tr, motifRng);

        // The per-segment budget still shapes the phrase (BUILD ramps,
        // Establish stays plain, Accelerate earns extra weight).
        const int trajSeg = bars > 1 ? mseg : mseg * 2;
        const float keepP = juce::jlimit (0.25f, 1.0f,
            0.35f + 0.6f * traj.at (traj.density, trajSeg) * roleBudget (role));
        for (auto e : placed.events)
        {
            if (motifRng.nextFloat() > keepP)
                continue;
            e.pos += mseg * motifSegLen;
            if (e.pos >= steps - 0.25)
                continue;
            if (style.interlocking && stepOccupied (p.events, e.pos))
                continue;
            if (hasEventAt (p.events, e.pos, e.role))
                continue;
            if (offbeatStackFull (e.pos))
                continue;   // avoid collision: the next voice must wait
            if (SilencePlanner::blocked (silence, e.pos, e.role == leadRole))
                continue;   // planned air is untouchable
            p.events.push_back (e);
        }
    }

    if (settings.density < 0.35f)
    {
        const float dropChance = (0.35f - settings.density) * 2.2f;
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
            [&] (const Event& e)
            { return ! e.protectedAnchor && rng.chance (dropChance); }),
            p.events.end());
    }

    // --- 4. designed ghosts, segment-aware -------------------------------------
    if (! profile.sparse)
    {
        for (int step = 0; step < steps; ++step)
        {
            if (step % 4 == 0)
                continue;
            const double pos = (double) step;
            const int seg = segOf (pos);
            const auto role = segRole (seg);
            if (role == PhraseRole::Breath || role == PhraseRole::Vacuum)
                continue;
            if (traj.at (traj.space, seg) > 0.7f)
                continue;
            // Space-aware texture: a phrase that wants air gets fewer ghosts
            // everywhere, not only in its planned quiet segments.
            const float ghostP = style.ghostiness
                * (0.12f + 0.8f * settings.density)
                * traj.at (traj.density, seg) * roleBudget (role)
                * (1.0f - 0.5f * feel.space);
            if (stepOccupied (p.events, pos) || ! rng.chance (ghostP))
                continue;
            // The arabic/mediterranean "ka" hugs a real hit; a pad-like
            // sample (no attack) cannot articulate a tick at all.
            if (style.ghostsNeedNeighbor && ! hasNeighbor (pos))
                continue;
            if (traits[(size_t) leadRole].weakTransient)
                continue;
            if (offbeatStackFull (pos))
                continue;
            if (SilencePlanner::blocked (silence, pos, true))
                continue;
            Event g;
            g.pos = pos;
            g.role = leadRole;
            g.velocity = 0.10f + 0.15f * rng.uni();
            g.gateSteps = 0.5;
            p.events.push_back (g);
        }

        // High macro density opens the 1/32 grid: the 16th slots saturate
        // (measured - the knob's top half moved the loop by three events),
        // so past 0.6 the ghost layer may also tick BETWEEN the 16ths.
        // Quieter than the on-grid ghosts, same guards, never in styles
        // whose ghosts must hug a neighbor.
        if (settings.density > 0.6f && ! style.ghostsNeedNeighbor
            && ! traits[(size_t) leadRole].weakTransient)
        {
            const float halfP = style.ghostiness * (settings.density - 0.6f) * 1.4f;
            for (int step = 0; step < steps; ++step)
            {
                const double pos = step + 0.5;
                const int seg = segOf (pos);
                const auto role = segRole (seg);
                if (role == PhraseRole::Breath || role == PhraseRole::Vacuum)
                    continue;
                if (traj.at (traj.space, seg) > 0.7f)
                    continue;
                if (stepOccupied (p.events, pos) || ! rng.chance (halfP))
                    continue;
                if (offbeatStackFull (pos))
                    continue;
                if (SilencePlanner::blocked (silence, pos, true))
                    continue;
                Event g;
                g.pos = pos;
                g.role = leadRole;
                g.velocity = 0.08f + 0.10f * rng.uni();
                g.gateSteps = 0.4;
                p.events.push_back (g);
            }
        }
    }

    // --- 5. randomness (same vocabulary as v1) ---------------------------------
    const float r = settings.randomness;
    for (auto& e : p.events)
    {
        if (e.protectedAnchor)
            continue;
        if (rng.chance (r * 0.4f))
            e.pos = juce::jlimit (0.0, (double) steps - 1.0, e.pos + (rng.chance (0.5f) ? 1.0 : -1.0));
        if (rng.chance (r * 0.25f) && e.role != Role::LOW)
            e.role = e.role == Role::HIGH ? Role::MID : Role::HIGH;
        if (rng.chance (r * 0.15f))
            e.pitchSemis = rng.chance (0.5f) ? 2 : -2;
    }
    if (r > 0.15f && ! traits[(size_t) Role::HIGH].weakTransient)
    {
        std::vector<Event> graces;
        for (const auto& e : p.events)
            if (e.role == Role::HIGH && ! e.protectedAnchor && e.pos >= 1.0
                && rng.chance (r * 0.3f))
            {
                Event g = e;
                g.pos -= 0.5;
                g.velocity *= 0.5f;
                g.gateSteps = 0.5;
                graces.push_back (g);
            }
        p.events.insert (p.events.end(), graces.begin(), graces.end());
    }

    // --- 6. call & response, now scoped to the planned segments ----------------
    for (int seg = 0; seg + 1 < plan.segments(); ++seg)
    {
        if (segRole (seg) != PhraseRole::Call
            || segRole (seg + 1) != PhraseRole::Response)
            continue;
        const double segLen = plan.beatLevel ? 4.0 : 16.0;
        const double from = (seg + 1) * segLen, to = (seg + 2) * segLen;
        for (auto& e : p.events)
            if (e.pos >= from && e.pos < to && ! e.protectedAnchor)
            {
                if (rng.chance (0.3f))
                    e.velocity = juce::jlimit (0.05f, 1.0f,
                        e.velocity + (rng.chance (0.5f) ? 0.2f : -0.15f));
                if (rng.chance (0.2f))
                    e.pos = juce::jmin ((double) steps - 1.0, e.pos + 0.5);
            }
    }

    // --- 7. energy + groove + the tension arc ----------------------------------
    const float energy = settings.energy;
    for (auto& e : p.events)
    {
        const bool onBeat = std::fmod (e.pos, 4.0) < 0.01;
        float v = e.velocity;
        // The knob has to be FELT: measured before this widening, the whole
        // ENERGY range moved mean velocity by 0.13 (~2 dB) - inaudible as a
        // macro. Low energy now genuinely calms the frame, high energy heats
        // it, and decoration drops much faster than anchors when it falls.
        v *= 0.82f + 0.36f * energy;
        if (onBeat)
            v *= 0.78f + 0.45f * energy;
        else
            v *= 1.0f - profile.velocityContrast * 0.55f * (1.0f - energy);

        const int mapStep = juce::jlimit (0, 15,
            juce::roundToInt (std::floor (e.pos)) % 16);
        const float mapW = style.accentMap[(size_t) mapStep];
        v *= e.protectedAnchor ? 0.6f + 0.4f * mapW : mapW;

        // The phrase moves: each segment tilts velocity by its tension, and
        // the lead voice follows the brightness curve.
        const int seg = segOf (e.pos);
        v *= 0.88f + 0.24f * traj.at (traj.tensionArc, seg);
        if (e.role == Role::HIGH)
            v *= 0.9f + 0.2f * traj.at (traj.brightness, seg);

        e.velocity = juce::jlimit (0.05f, 1.0f, v);

        if (e.role == Role::LOW && energy > 0.7f && rng.chance (0.2f))
            e.pitchSemis = -1;
        if (! e.protectedAnchor)
        {
            // Correlated performance, not blind jitter (Phase 6). Everything
            // scales with randomness x the family's looseness, so r=0 stays
            // machine-tight (feel offsets only) and psytrance never loosens.
            const float human = r * feel.looseness * 3.4f;
            float micro = (rng.uni() * 2.0f - 1.0f) * style.timingVarianceMs * human;
            // Slow breathing across the phrase - deterministic in position.
            micro += std::sin ((float) (e.pos / steps)
                               * juce::MathConstants<float>::twoPi)
                     * style.driftMs * human;
            // Velocity/timing correlation: confident hits arrive a touch
            // early, ghosts hang behind.
            micro -= (e.velocity - 0.5f) * style.velTimingCorrMs * r;
            if (e.velocity <= 0.28f)
                micro += style.ghostLagMs * human;
            if (e.role == Role::HIGH)
                micro += style.highFeelMs;
            else if (e.role == Role::MID)
                micro += style.midFeelMs;
            e.microMs = juce::jlimit (-12.0f, 12.0f, micro);
        }
    }

    // DROP breathing, as in v1.
    if (settings.mode == Mode::DROP && rng.chance (profile.silenceChance + 0.3f * energy))
    {
        const double holeStart = steps - (rng.chance (0.5f) ? 1.0 : 2.0);
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
            [&] (const Event& e) { return e.pos >= holeStart && ! (e.pos == 0.0); }),
            p.events.end());
    }

    // --- 8. endings: the plan decides between roll and vacuum ------------------
    const bool planVacuum = plan.roles.back() == PhraseRole::Vacuum;
    const float fillP = juce::jlimit (0.0f, 0.95f,
        profile.fillChance + (settings.mode == Mode::DROP ? 0.25f * energy : 0.0f));

    if (! planVacuum && rng.chance (fillP))
    {
        // Which GESTURE closes the phrase. A fill does not have to cover the
        // whole bar: sparse-drumming practice and film percussion both place
        // a few well-chosen hits and leave the rest as air - the space reads
        // as groove where a wall-to-wall roll reads as filler. Three shapes:
        //   FullRoll - the classic roll into the downbeat (v1 vocabulary)
        //   Touches  - two or three isolated hits in the final bar with real
        //              space between them, and the "and" of beat 4 cleared
        //              on purpose, so the bar breathes before the one
        //   Pickup   - air until a short 1-3 hit throw right before the
        //              loop restarts
        // BUILD keeps the accelerating roll: its whole job is momentum.
        enum class FillGesture { FullRoll, Touches, Pickup };
        FillGesture gesture = FillGesture::FullRoll;
        if (settings.mode != Mode::BUILD)
        {
            const float g = rng.uni();
            if (settings.mode == Mode::BREAK || settings.mode == Mode::GROOVE)
                gesture = g < 0.40f ? FillGesture::Touches
                        : g < 0.62f ? FillGesture::Pickup
                                    : FillGesture::FullRoll;
            else
                gesture = g < 0.30f ? FillGesture::Touches : FillGesture::FullRoll;
        }

        // A weak-transient sample cannot articulate a roll: the gesture goes
        // to the other voice (and if both are weak, the fill is skipped by
        // the spacing pass below - swells serve those samples instead).
        Role rollRole = rng.chance (0.4f) ? Role::MID : Role::HIGH;
        if (traits[(size_t) rollRole].weakTransient)
            rollRole = rollRole == Role::MID ? Role::HIGH : Role::MID;

        if (gesture == FillGesture::FullRoll)
        {
            const int rollSteps = 2 + rng.pick (settings.mode == Mode::BUILD ? 3 : 2);
            const double rollStart = steps - rollSteps;
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [&] (const Event& e)
                { return e.pos >= rollStart && e.role == rollRole && ! e.protectedAnchor; }),
                p.events.end());

            const bool accelerate = settings.mode == Mode::BUILD;
            double spacing = accelerate ? 0.9 : rng.chance (0.3f) ? 1.0 / 3.0 : 0.5;
            double pos = rollStart;
            while (pos < steps - 0.25)
            {
                Event e;
                e.pos = pos;
                e.role = rollRole;
                e.roll = true;
                const float t = (float) (pos - rollStart) / (float) rollSteps;
                e.velocity = juce::jlimit (0.1f, 1.0f, 0.35f + 0.6f * t * (0.5f + 0.5f * energy));
                e.gateSteps = spacing;
                if (settings.mode == Mode::BUILD)
                    e.pitchSemis = snapToScale (t * (3.0f + 5.0f * energy));
                p.events.push_back (e);
                pos += spacing;
                if (accelerate)
                    spacing = juce::jmax (0.25, spacing * 0.8);
            }
        }
        else if (gesture == FillGesture::Touches)
        {
            // The final bar keeps its anchors; this role's decoration clears
            // out and a few touches take its place, ON musically strong spots
            // that are NOT the downbeat and NOT the "and" of 4.
            const double barStart = steps - 4.0;
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [&] (const Event& e)
                { return e.pos >= barStart && e.role == rollRole && ! e.protectedAnchor; }),
                p.events.end());

            static constexpr double menu[5] = { 0.5, 1.0, 2.0, 2.5, 3.0 };
            const int count = 2 + rng.pick (2);        // 2-3 touches
            int idx = rng.pick (2);                    // enter early or mid-bar
            const Role alt = rollRole == Role::MID ? Role::HIGH : Role::MID;
            double lastPlaced = -9.0;
            int placed = 0;
            for (int k = 0; k < count && idx < 5; ++k)
            {
                const double pos = barStart + menu[idx];
                if (pos - lastPlaced >= 0.99 && ! stepOccupied (p.events, pos))
                {
                    Event e;
                    e.pos = pos;
                    e.role = (placed % 2 == 1 && ! traits[(size_t) alt].weakTransient)
                                 ? alt : rollRole;
                    e.roll = true;
                    e.velocity = 0.45f + 0.14f * (float) placed + 0.1f * rng.uni();
                    e.gateSteps = 1.0;
                    p.events.push_back (e);
                    lastPlaced = pos;
                    ++placed;
                }
                idx += 1 + rng.pick (2);
            }
            // The breath: nothing but anchors may sit on the "and" of beat 4.
            // An empty half-step right before the one is what makes the
            // touches read as intention instead of a thin roll.
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [steps] (const Event& e)
                { return e.pos >= steps - 0.75 && ! e.protectedAnchor; }),
                p.events.end());
        }
        else // FillGesture::Pickup
        {
            // Air first, then the throw: the last two steps empty out except
            // anchors, and a 1-3 hit pickup on the 1/32 grid leans into the
            // loop restart with rising weight.
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [steps] (const Event& e)
                { return e.pos >= steps - 2.0 && ! e.protectedAnchor; }),
                p.events.end());
            const int hits = 1 + rng.pick (3);         // 1-3
            for (int k = 0; k < hits; ++k)
            {
                Event e;
                e.pos = steps - 0.5 * (hits - k);
                e.role = rollRole;
                e.roll = true;
                e.velocity = juce::jlimit (0.2f, 1.0f,
                    0.45f + 0.55f * (float) (k + 1) / (float) hits);
                e.gateSteps = 0.5;
                p.events.push_back (e);
            }
        }
    }

    // --- 9. transition drama (v1 vocabulary, plan-aware) -----------------------
    if (settings.mode == Mode::BUILD)
    {
        if (rng.chance (0.65f))
        {
            const bool descending = rng.chance (0.6f);
            std::vector<Event*> countdown;
            for (auto& e : p.events)
                if (e.role == Role::LOW && e.protectedAnchor
                    && e.pos >= steps - 16 && std::fmod (e.pos, 4.0) < 0.01)
                    countdown.push_back (&e);
            const int n = (int) countdown.size();
            for (int k = 0; k < n; ++k)
            {
                const float t = n > 1 ? (float) k / (float) (n - 1) : 1.0f;
                const int walk = snapToScale (t * (5.0f + 4.0f * energy));
                countdown[(size_t) k]->pitchSemis += descending ? -walk : walk;
                countdown[(size_t) k]->velocity =
                    juce::jlimit (0.05f, 1.0f, 0.8f + 0.2f * t);
            }
        }
        if (planVacuum)
        {
            // The planned vacuum: the last segment empties, one accented
            // pickup may throw into the entrance.
            const double segLen = plan.beatLevel ? 4.0 : 16.0;
            const double holeStart = steps - segLen * 0.5;
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [holeStart] (const Event& e) { return e.pos >= holeStart; }),
                p.events.end());
            if (rng.chance (0.7f))
            {
                Event pickup;
                pickup.pos = steps - 1.0;
                pickup.role = Role::LOW;
                pickup.velocity = 1.0f;
                pickup.pitchSemis = rng.chance (0.4f) ? -2 : 0;
                pickup.protectedAnchor = true;
                p.events.push_back (pickup);
            }
        }
    }

    if (settings.mode == Mode::BREAK)
    {
        if (rng.chance (0.45f))
        {
            Event swell;
            swell.pos = steps - 3.75;
            swell.role = Role::HIGH;
            swell.reverse = true;
            swell.gateSteps = 3.45;
            swell.velocity = 0.5f + 0.25f * energy;
            p.events.push_back (swell);
        }
        if (rng.chance (0.35f))
        {
            const double pos = 4.0 + rng.pick (juce::jmax (1, steps - 8));
            if (! stepOccupied (p.events, pos))
            {
                Event deep;
                deep.pos = pos;
                deep.role = Role::LOW;
                deep.velocity = 0.85f;
                deep.pitchSemis = -5;
                deep.gateSteps = 3.0;
                p.events.push_back (deep);
            }
        }
    }

    // --- 9a. micro-risers ------------------------------------------------------
    // A short run that starts LOW and climbs HIGH: 1/16s tightening into
    // 1/32s, velocity soft-to-full, pitch walking up a few semitones - the
    // snare-rush gesture at loop scale. It always ends ON a boundary (the
    // loop restart, or the half-phrase of a 4-bar loop) so the rise throws
    // into an arrival instead of hanging in the air. DROP is excluded: its
    // job is release, and it already breathes a hole at the phrase end.
    if (settings.mode != Mode::DROP && ! planVacuum)
    {
        const bool wantRiser =
            settings.mode == Mode::BUILD  ? rng.chance (0.5f + 0.3f * energy)
          : settings.mode == Mode::BREAK  ? rng.chance (0.25f)
                                          : rng.chance (0.18f);   // GROOVE
        if (wantRiser)
        {
            Role riserRole = rng.chance (0.6f) ? Role::MID : Role::HIGH;
            if (traits[(size_t) riserRole].weakTransient)
                riserRole = riserRole == Role::MID ? Role::HIGH : Role::MID;

            const double target = (bars == 4 && rng.chance (0.4f))
                                      ? 32.0 : (double) steps;
            const double runLen = 2.0 + rng.pick (3);   // 2-4 steps of run
            const bool tighten = rng.chance (0.65f);    // 1/16 -> 1/32 accel
            double spacing = tighten ? 1.0
                           : rng.chance (0.5f) ? 1.0 : 0.5;
            const int lowStart = -4 - rng.pick (3);
            const int highEnd = 4 + juce::roundToInt (4.0f * energy);

            std::vector<double> at;
            for (double pos = target - runLen; pos < target - 0.26; )
            {
                at.push_back (pos);
                pos += spacing;
                if (tighten)
                    spacing = juce::jmax (0.5, spacing * 0.7);
            }
            for (size_t k = 0; k < at.size(); ++k)
            {
                const float t = at.size() > 1
                    ? (float) k / (float) (at.size() - 1) : 1.0f;
                Event e;
                e.pos = at[k];
                e.role = riserRole;
                e.roll = true;
                e.velocity = juce::jlimit (0.1f, 1.0f, 0.3f + 0.65f * t);
                e.pitchSemis = snapToScale (
                    (float) lowStart + t * (float) (highEnd - lowStart));
                e.gateSteps = 0.5;
                p.events.push_back (e);
            }
        }
    }

    // 4-bar mid-phrase answer fill, as in v1, but never into planned air.
    if (bars == 4 && segRole (1) != PhraseRole::Breath
        && rng.chance (0.25f + profile.fillChance * 0.4f))
    {
        Role halfRole = rng.chance (0.5f) ? Role::MID : Role::HIGH;
        if (traits[(size_t) halfRole].weakTransient)
            halfRole = halfRole == Role::MID ? Role::HIGH : Role::MID;
        for (double pos = 30.5; pos < 31.9; pos += 0.5)
        {
            Event e;
            e.pos = pos;
            e.role = halfRole;
            e.roll = true;
            e.velocity = 0.3f + 0.2f * rng.uni();
            e.gateSteps = 0.5;
            p.events.push_back (e);
        }
    }

    // --- 9b. destination-aware endings (Phase 7 / A8 shared helper) -------------
    applyEnding (p, destination, traits, ornamentSeed ^ 0xE4D1E4D1ull);

    std::sort (p.events.begin(), p.events.end(), eventBefore);

    // --- 9c. Sample Intelligence: tonal voices become MELODIC ------------------
    // A lead loaded into a slot should not be treated as a snare. Every
    // tonal (non-pad) role gets a deterministic melody: its hits walk the
    // natural-minor ladder with repetition and homecoming - downbeats return
    // to the root - so ONE note sample becomes a riff. The melody belongs to
    // the MOTIF seed (its own stream, no interference with existing draws):
    // reroll keeps the tune and changes only the performance.
    for (int ri : { (int) Role::MID, (int) Role::HIGH, (int) Role::FX })
    {
        const auto& tr = traits[(size_t) ri];
        if (! tr.tonal || tr.padLike)
            continue;
        // The tune is a fixed LINE over the step grid, drawn once from the
        // motif stream - events SAMPLE it by position. Deriving pitches from
        // event order instead would let the ornament seed reshuffle the
        // melody on every reroll (caught by test).
        Rng mel (motifSeed ^ (0x7071A1ull * (juce::uint64) (ri + 1)));
        static constexpr int melLadder[] = { 0, 2, 3, 5, 7, 8, 10, 12 };
        std::array<int, 64> line {};
        int idx = 0;
        for (int st = 0; st < juce::jmin (64, steps); ++st)
        {
            if (st % 16 == 0)
                idx = 0;                               // bar line: home
            else
            {
                const float u = mel.uni();
                if (u < 0.35f)      { /* hold the note */ }
                else if (u < 0.78f) idx += mel.chance (0.55f) ? 1 : -1;
                else                idx += mel.chance (0.5f) ? 2 : -2;
                idx = juce::jlimit (0, 7, idx);
            }
            line[(size_t) st] = idx;
        }
        for (auto& e : p.events)
        {
            if ((int) e.role != ri)
                continue;
            const int st = juce::jlimit (0, juce::jmin (63, steps - 1),
                                         (int) std::floor (e.pos));
            e.pitchSemis = melLadder[line[(size_t) st]];
            if (e.gateSteps <= 0.0 || e.gateSteps > 2.0)
                e.gateSteps = 1.5;                     // notes, not ticks
            e.roll = false;                            // runs become lines
        }
    }

    // --- 10. sample-aware spacing pass -----------------------------------------
    // The symbolic result must already respect what the audio can carry:
    //  - a sustained sample gets gated everywhere and never re-fires inside
    //    a single step (the render-side choke handles the rest)
    //  - a low-heavy sample never doubles up inside a single step
    //  - weak-transient roles lose their rolls (they smear, not snap)
    {
        std::array<double, 5> lastPos { -99.0, -99.0, -99.0, -99.0, -99.0 };
        std::vector<Event> kept;
        kept.reserve (p.events.size());
        for (auto e : p.events)
        {
            const auto& t = traits[(size_t) e.role];
            // Pads breathe: two steps between notes, long gates.
            const double minGap = t.padLike ? 2.0
                                : t.sustained || t.lowHeavy ? 1.0 : 0.0;
            const bool tooClose = minGap > 0.0 && ! e.protectedAnchor && ! e.roll
                && e.pos - lastPos[(size_t) e.role] < minGap - 1.0e-9;
            if (tooClose)
                continue;
            if (t.weakTransient && e.roll)
                continue;
            if (t.padLike && e.gateSteps < 3.0)
                e.gateSteps = 3.0;
            else if (t.sustained && e.gateSteps <= 0.0)
                e.gateSteps = 2.0;
            lastPos[(size_t) e.role] = e.pos;
            kept.push_back (e);
        }
        p.events = std::move (kept);

        // Planned silence survives every later stage: whatever randomness,
        // graces or call/response shifted into a protected region is removed.
        p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
            [&] (const Event& e)
            {
                return ! e.protectedAnchor && ! e.roll
                    && SilencePlanner::blocked (silence, e.pos,
                                                e.role == leadRole);
            }),
            p.events.end());

        // The ka rule holds to the END: after every shift and hole, a ghost
        // in a neighbor-grammar style must still hug a real (non-ghost) hit.
        if (style.ghostsNeedNeighbor)
        {
            auto isGhost = [] (const Event& e)
            { return e.velocity <= 0.28f && e.gateSteps == 0.5 && ! e.roll; };
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [&] (const Event& g)
                {
                    if (! isGhost (g))
                        return false;
                    for (const auto& o : p.events)
                        if (! isGhost (o) && std::abs (o.pos - g.pos) <= 1.01)
                            return false;
                    return true;
                }),
                p.events.end());
        }
    }
    return p;
}


void LoopGenerator::applyEnding (Pattern& p, Destination destination,
                                 const TraitsByRole& traits,
                                 juce::uint64 choiceSeed)
{
    const int steps = p.stepCount();
    juce::Random rng ((juce::int64) choiceSeed);
    p.destination = destination;
    switch (destination)
    {
        case Destination::LoopBack:
            break;
        case Destination::ToDrop:
        {
            // Throw INTO the next section: an accented pickup on the last
            // 16th, always - the ending must promise the impact.
            Event pickup;
            pickup.pos = steps - 1.0;
            pickup.role = Role::LOW;
            pickup.velocity = 1.0f;
            pickup.protectedAnchor = true;
            bool exists = false;
            for (const auto& e : p.events)
                if (e.role == Role::LOW && std::abs (e.pos - pickup.pos) < 0.26)
                    exists = true;
            if (! exists)
                p.events.push_back (pickup);
            break;
        }
        case Destination::ToBreak:
        {
            // Deflate: the decoration leaves the last two steps, and a
            // reverse swell (if the sample can carry it) pulls the air open.
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [steps] (const Event& e)
                { return e.pos >= steps - 2.0 && ! e.protectedAnchor; }),
                p.events.end());
            if (! traits[(size_t) Role::HIGH].weakTransient
                && rng.nextFloat() < 0.8f)
            {
                Event swell;
                swell.pos = steps - 3.75;
                swell.role = Role::HIGH;
                swell.reverse = true;
                swell.gateSteps = 3.45;
                swell.velocity = 0.55f;
                p.events.push_back (swell);
            }
            break;
        }
        case Destination::ToStop:
        {
            // The intentional stop: one final accent, then nothing.
            p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
                [steps] (const Event& e) { return e.pos >= steps - 4.0; }),
                p.events.end());
            Event last;
            last.pos = steps - 4.0;
            last.role = Role::LOW;
            last.velocity = 1.0f;
            last.gateSteps = 3.0;
            last.protectedAnchor = true;
            p.events.push_back (last);
            break;
        }
    }
    std::sort (p.events.begin(), p.events.end(), eventBefore);
}

void LoopGenerator::cleanPattern (Pattern& p, int strength)
{
    // Removal only - anchors, the destination ending's protected events and
    // planned silence are structurally safe (nothing is ever ADDED here).
    strength = juce::jlimit (1, 3, strength);
    p.events.erase (std::remove_if (p.events.begin(), p.events.end(),
        [strength] (const Event& e)
        {
            if (e.protectedAnchor)
                return false;
            const bool ghost = e.velocity <= 0.28f && e.gateSteps == 0.5 && ! e.roll;
            const bool grace = ! e.roll
                && std::abs (e.pos - std::round (e.pos)) > 0.01
                && e.velocity < 0.45f && ! e.reverse;
            if (strength >= 1 && (ghost || grace))
                return true;
            const bool quietOrnament = e.gateSteps == 0.75 && e.velocity < 0.45f;
            const bool softRoll = e.roll && e.velocity < 0.4f;
            if (strength >= 2 && (quietOrnament || softRoll))
                return true;
            if (strength >= 3 && ! e.roll && ! e.reverse && e.velocity < 0.55f)
                return true;
            return false;
        }),
        p.events.end());
}

} // namespace orcha
