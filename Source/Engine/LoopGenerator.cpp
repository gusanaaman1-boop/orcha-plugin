#include "LoopGenerator.h"
#include "PatternValidator.h"
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

    // --- 8. energy: velocity contour, accents, pre-impact silence -------------
    const float energy = settings.energy;
    for (auto& e : p.events)
    {
        const bool onBeat = std::fmod (e.pos, 4.0) < 0.01;
        float v = e.velocity;
        if (onBeat)
            v *= 0.85f + 0.35f * energy;                       // accent lift
        else
            v *= 1.0f - profile.velocityContrast * 0.35f * (1.0f - energy);
        if (profile.densityRamp)                               // BUILD contour
            v *= 0.55f + 0.45f * (float) (e.pos / steps);
        e.velocity = juce::jlimit (0.05f, 1.0f, v);

        // Weight at high energy: occasionally drop the LOW pitch for impact.
        if (e.role == Role::LOW && energy > 0.7f && rng.chance (0.2f))
            e.pitchSemis = -1;
        // Humanize timing, but never the structural anchors.
        if (! e.protectedAnchor)
            e.microMs = (rng.uni() * 2.0f - 1.0f) * 3.0f * r;
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
    if (rng.chance (fillP))
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
                e.pitchSemis = (int) (t * 4.0f);   // rising tension
            p.events.push_back (e);

            pos += spacing;
            if (accelerate)
                spacing = juce::jmax (0.25, spacing * 0.8);
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
    std::sort (p.events.begin(), p.events.end(),
               [] (const Event& a, const Event& b) { return a.pos < b.pos; });
    return p;
}

} // namespace orcha
