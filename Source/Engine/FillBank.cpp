#include "FillBank.h"

namespace orcha
{

namespace
{
    struct Rng
    {
        explicit Rng (juce::uint64 seed) : r ((juce::int64) seed) {}
        float uni()               { return r.nextFloat(); }
        bool  chance (float p)    { return r.nextFloat() < p; }
        int   pick (int n)        { return r.nextInt (juce::jmax (1, n)); }
        juce::Random r;
    };

    // One authored hit inside a template. Positions are 16th steps 0..16.
    struct THit
    {
        double pos;
        Role   role;
        float  vel;
        double gate     = 0.0;
        int    pitch    = 0;
        bool   reverse  = false;
        bool   roll     = false;
    };

    struct FillTemplate
    {
        const char* name;
        bool chopped;              // false = PLAYED, true = CHOPPED
        std::vector<THit> hits;
    };

    // Helpers that expand gestures into hits, so the table below stays
    // readable. All deterministic - no rng here; shading happens later.
    std::vector<THit> ramp (Role role, double from, double to, double spacing,
                            float v0, float v1, int pitchTo = 0)
    {
        std::vector<THit> out;
        const int n = juce::jmax (1, (int) std::floor ((to - from) / spacing));
        for (int i = 0; i < n; ++i)
        {
            const float t = n > 1 ? (float) i / (float) (n - 1) : 1.0f;
            out.push_back ({ from + i * spacing, role,
                             v0 + (v1 - v0) * t, spacing,
                             pitchTo == 0 ? 0 : snapToScale (pitchTo * t),
                             false, true });
        }
        return out;
    }

    std::vector<THit> stutter (Role role, double at, int repeats, double rate,
                               float v0, float v1)
    {
        std::vector<THit> out;
        for (int i = 0; i < repeats; ++i)
        {
            const float t = repeats > 1 ? (float) i / (float) (repeats - 1) : 1.0f;
            out.push_back ({ at + i * rate, role, v0 + (v1 - v0) * t,
                             rate, 0, false, true });
        }
        return out;
    }

    std::vector<THit> cat (std::initializer_list<std::vector<THit>> parts)
    {
        std::vector<THit> out;
        for (const auto& p : parts)
            out.insert (out.end(), p.begin(), p.end());
        return out;
    }

    // The bank. Authored, not sampled: these are placement grammars in the
    // drummer/producer traditions, played by whatever the user loaded.
    const std::vector<FillTemplate>& bank()
    {
        static const std::vector<FillTemplate> t = []
        {
            std::vector<FillTemplate> b;
            using R = Role;

            // ---------------- PLAYED ----------------
            b.push_back ({ "snare-ramp", false, cat ({
                { { 0.0, R::LOW, 1.0f }, { 4.0, R::MID, 0.75f } },
                ramp (R::MID, 8.0, 12.0, 1.0, 0.45f, 0.7f),
                ramp (R::MID, 12.0, 16.0, 0.5, 0.6f, 1.0f) }) });

            b.push_back ({ "tom-cascade", false, cat ({
                { { 0.0, R::LOW, 1.0f } },
                ramp (R::MID, 6.0, 16.0, 1.0, 0.9f, 0.6f, -7) }) });

            b.push_back ({ "kick-snare-trade", false, {
                { 0.0, R::LOW, 1.0f }, { 2.0, R::MID, 0.7f },
                { 4.0, R::LOW, 0.85f }, { 6.0, R::MID, 0.8f },
                { 8.0, R::LOW, 0.9f }, { 9.5, R::MID, 0.6f },
                { 10.0, R::MID, 0.75f }, { 12.0, R::LOW, 0.95f },
                { 13.0, R::MID, 0.7f }, { 14.0, R::MID, 0.85f },
                { 15.0, R::MID, 1.0f } } });

            b.push_back ({ "crescendo-roll", false, cat ({
                { { 0.0, R::LOW, 0.9f } },
                ramp (R::MID, 8.0, 16.0, 0.5, 0.25f, 1.0f) }) });

            b.push_back ({ "hat-throw", false, cat ({
                { { 0.0, R::LOW, 1.0f }, { 4.0, R::MID, 0.8f },
                  { 8.0, R::LOW, 0.85f } },
                ramp (R::HIGH, 10.0, 16.0, 0.5, 0.4f, 0.95f) }) });

            b.push_back ({ "triplet-turn", false, cat ({
                { { 0.0, R::LOW, 1.0f }, { 4.0, R::MID, 0.75f },
                  { 8.0, R::LOW, 0.8f } },
                ramp (R::MID, 12.0, 16.0, 4.0 / 6.0, 0.6f, 1.0f) }) });

            b.push_back ({ "ghost-swarm", false, cat ({
                { { 0.0, R::LOW, 1.0f } },
                ramp (R::HIGH, 4.0, 14.0, 1.0, 0.15f, 0.35f),
                { { 14.0, R::MID, 0.9f }, { 15.0, R::MID, 1.0f } } }) });

            b.push_back ({ "backbeat-push", false, {
                { 0.0, R::LOW, 1.0f }, { 4.0, R::MID, 0.85f },
                { 7.0, R::LOW, 0.7f }, { 8.0, R::LOW, 0.9f },
                { 11.0, R::MID, 0.75f }, { 12.0, R::MID, 0.85f },
                { 14.0, R::MID, 0.95f }, { 15.5, R::HIGH, 0.8f, 0.5 } } });

            b.push_back ({ "double-time-snap", false, cat ({
                { { 0.0, R::LOW, 1.0f }, { 2.0, R::MID, 0.7f },
                  { 4.0, R::LOW, 0.8f }, { 6.0, R::MID, 0.75f } },
                ramp (R::MID, 8.0, 12.0, 1.0, 0.6f, 0.8f),
                ramp (R::MID, 12.0, 16.0, 0.5, 0.7f, 1.0f) }) });

            b.push_back ({ "quiet-touch", false, {
                { 0.0, R::LOW, 1.0f }, { 6.0, R::MID, 0.55f },
                { 10.0, R::HIGH, 0.5f, 1.0 }, { 13.0, R::MID, 0.8f } } });

            // ---------------- CHOPPED ----------------
            b.push_back ({ "stutter-eight", true, cat ({
                { { 0.0, R::LOW, 1.0f }, { 4.0, R::MID, 0.8f },
                  { 8.0, R::LOW, 0.85f } },
                stutter (R::MID, 12.0, 8, 0.5, 0.5f, 1.0f) }) });

            b.push_back ({ "gate-chop", true, {
                { 0.0, R::LOW, 1.0f, 0.5 }, { 2.0, R::LOW, 0.9f, 0.5 },
                { 4.0, R::LOW, 0.95f, 0.5 }, { 6.0, R::MID, 0.85f, 0.5 },
                { 8.0, R::LOW, 0.9f, 0.5 }, { 10.0, R::MID, 0.8f, 0.5 },
                { 12.0, R::MID, 0.9f, 0.5 }, { 14.0, R::MID, 1.0f, 0.5 } } });

            b.push_back ({ "reverse-suck", true, {
                { 0.0, R::LOW, 1.0f }, { 4.0, R::MID, 0.8f },
                { 8.0, R::LOW, 0.85f },
                { 12.0, R::HIGH, 0.85f, 3.9, 0, true },
                { 15.0, R::MID, 1.0f } } });

            b.push_back ({ "pitch-stairs", true, cat ({
                { { 0.0, R::LOW, 1.0f } },
                ramp (R::MID, 4.0, 16.0, 1.0, 0.85f, 0.85f, -12) }) });

            b.push_back ({ "glitch-scatter", true, {
                { 0.0, R::LOW, 1.0f }, { 3.5, R::HIGH, 0.6f, 0.5 },
                { 5.0, R::MID, 0.7f, 0.5 }, { 6.75, R::HIGH, 0.55f, 0.25 },
                { 9.0, R::MID, 0.8f, 0.5 }, { 10.25, R::HIGH, 0.6f, 0.25 },
                { 12.5, R::MID, 0.85f, 0.5 }, { 13.75, R::MID, 0.9f, 0.25 },
                { 15.0, R::MID, 1.0f } } });

            b.push_back ({ "rise-and-cut", true, cat ({
                { { 0.0, R::LOW, 1.0f } },
                ramp (R::MID, 4.0, 12.0, 0.5, 0.3f, 0.95f, 7) }) });
                // steps 12..16 stay EMPTY: the vacuum before the next one.

            b.push_back ({ "halftime-smash", true, cat ({
                { { 0.0, R::LOW, 1.0f }, { 8.0, R::MID, 1.0f, 2.0 } },
                stutter (R::MID, 14.0, 4, 0.5, 0.6f, 1.0f) }) });

            b.push_back ({ "stutter-fall", true, cat ({
                { { 0.0, R::LOW, 1.0f } },
                stutter (R::LOW, 8.0, 4, 1.0, 0.9f, 0.7f),
                stutter (R::MID, 12.0, 6, 0.5, 0.9f, 0.5f) }) });

            b.push_back ({ "double-stop", true, {
                { 0.0, R::LOW, 1.0f, 0.5 }, { 1.0, R::LOW, 0.8f, 0.5 },
                { 6.0, R::MID, 0.9f, 0.5 }, { 7.0, R::MID, 0.75f, 0.5 },
                { 12.0, R::LOW, 0.95f, 0.5 }, { 13.0, R::MID, 0.85f, 0.5 },
                { 14.0, R::MID, 0.95f, 0.5 }, { 15.0, R::HIGH, 0.9f, 0.5 } } });

            b.push_back ({ "vacuum-throw", true, {
                { 0.0, R::LOW, 1.0f }, { 2.0, R::MID, 0.75f },
                { 4.0, R::LOW, 0.85f },
                // silence from 5 to 14 - the cut itself is the gesture
                { 14.0, R::MID, 0.85f }, { 15.0, R::MID, 1.0f },
                { 15.5, R::HIGH, 0.9f, 0.5 } } });

            return b;
        }();
        return t;
    }
} // namespace

int FillBank::templateCount() { return (int) bank().size(); }

Pattern FillBank::build (juce::uint64 motifSeed, juce::uint64 ornamentSeed,
                         const GeneratorSettings& settings, const TraitsByRole& traits)
{
    Pattern p;
    p.seed = motifSeed;
    p.ornamentSeed = ornamentSeed;
    p.algo = 2;
    p.settings = settings;
    p.settings.mode = Mode::FILL;
    // The bars setting means LENGTH here, like the fill tools do it:
    //   1 -> a HALF-bar fill (the gesture double-time in the back half,
    //        front half silent - drop it on the last bar of a phrase)
    //   2 -> the classic ONE-bar fill
    //   4 -> a TWO-bar fill: bar one states the approach, bar two throws
    const int lengthSel = settings.bars;
    p.settings.bars = lengthSel >= 4 ? 2 : 1;
    p.swing = 0.0;

    Rng rngM (motifSeed);
    Rng rng (ornamentSeed);

    // The motif stream owns the fill's identity: which template, and which
    // voice leads when the template's role cannot articulate.
    const auto& all = bank();
    const auto& tpl = all[(size_t) rngM.pick ((int) all.size())];
    p.name = juce::String (tpl.name).replaceCharacter ('-', ' ').toUpperCase();

    const float energy = settings.energy;
    const float density = settings.density;
    const float r = settings.randomness;

    const double steps = p.stepCount();

    // Where the authored gesture lands, per length. Every template is
    // written over one bar (0..16); the length maps it onto the timeline.
    auto place = [&] (double pos) -> double
    {
        if (lengthSel <= 1)                 // half: double-time, back half
            return steps - 8.0 + pos * 0.5;
        if (lengthSel >= 4)                 // two bars: gesture owns bar two
            return 16.0 + pos;
        return pos;                         // one bar: verbatim
    };
    const double gateScale = lengthSel <= 1 ? 0.5 : 1.0;

    // Two-bar fills open with an APPROACH: the template's accents alone,
    // softened, across bar one - a drummer marking the phrase before the
    // fill proper.
    if (lengthSel >= 4)
        for (const auto& h : tpl.hits)
            if (h.vel >= 0.85f && ! h.reverse)
            {
                Event a;
                a.pos = h.pos;
                a.role = h.role;
                a.velocity = juce::jlimit (0.05f, 1.0f,
                    h.vel * 0.55f * (0.72f + 0.42f * energy));
                a.gateSteps = h.gate;
                a.roll = h.roll;
                p.events.push_back (a);
            }

    for (const auto& h : tpl.hits)
    {
        // Low macro density thins the inner body of the fill but never its
        // first beat or its throw into the next bar.
        const bool protectedPos = h.pos < 1.0 || h.pos >= 14.0;
        if (! protectedPos && density < 0.5f
            && rng.chance ((0.5f - density) * 1.2f))
            continue;

        Event e;
        e.pos = place (h.pos);
        e.role = h.role;
        // A weak-transient sample cannot articulate fast material: the hit
        // moves to the neighbouring voice rather than smearing.
        if (traits[(size_t) e.role].weakTransient && (h.roll || h.gate > 0.0))
            e.role = e.role == Role::MID ? Role::HIGH : Role::MID;
        e.velocity = juce::jlimit (0.05f, 1.0f,
            h.vel * (0.72f + 0.42f * energy) * (0.92f + 0.16f * rng.uni()));
        e.gateSteps = h.gate * gateScale;
        e.pitchSemis = h.pitch;
        e.reverse = h.reverse;
        e.roll = h.roll;
        e.microMs = (rng.uni() * 2.0f - 1.0f) * 2.5f * r;
        p.events.push_back (e);
    }

    // High density adds connective ghosts in the empty half-steps of the
    // fill's front half - the back half belongs to the template's gesture.
    if (density > 0.6f && lengthSel > 1)
    {
        const double from = lengthSel >= 4 ? 17.5 : 1.5;
        const double to = lengthSel >= 4 ? 24.0 : 8.0;
        for (double pos = from; pos < to; pos += 1.0)
        {
            bool taken = false;
            for (const auto& e : p.events)
                if (std::abs (e.pos - pos) < 0.3)
                    { taken = true; break; }
            if (taken || ! rng.chance ((density - 0.6f) * 1.5f))
                continue;
            Event g;
            g.pos = pos;
            g.role = Role::HIGH;
            g.velocity = 0.12f + 0.1f * rng.uni();
            g.gateSteps = 0.5;
            g.roll = true;
            p.events.push_back (g);
        }
    }

    // Randomness: the same vocabulary as the loop engine - position slips
    // and voice swaps on non-structural hits.
    const double slipLo = lengthSel <= 1 ? steps - 7.5 : 1.0;
    const double slipHi = steps - 2.0;
    for (auto& e : p.events)
    {
        if (e.pos < slipLo || e.pos >= slipHi)
            continue;
        if (rng.chance (r * 0.3f))
            e.pos = juce::jlimit (slipLo, slipHi, e.pos + (rng.chance (0.5f) ? 0.5 : -0.5));
        if (rng.chance (r * 0.2f) && e.role != Role::LOW)
            e.role = e.role == Role::HIGH ? Role::MID : Role::HIGH;
    }

    // The downbeat anchors; everything else already carries the gesture.
    for (auto& e : p.events)
        if (e.pos < 0.01 && e.role == Role::LOW)
            e.protectedAnchor = true;

    return p;
}

} // namespace orcha
