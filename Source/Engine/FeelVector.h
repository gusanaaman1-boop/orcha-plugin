#pragma once

#include "Pattern.h"
#include <array>

namespace orcha
{

// The internal musical state behind the three user macros. ENERGY, DENSITY
// and RANDOMNESS stay the only controls the user sees; each family and
// section interprets them differently, and that interpretation lives here -
// deterministically, derived only from (settings, motifSeed).
struct FeelVector
{
    float drive = 0.5f;        // forward push
    float tension = 0.4f;      // unresolved pressure
    float space = 0.4f;        // how much air the phrase keeps
    float syncopation = 0.4f;  // off-grid weight target
    float repetition = 0.6f;   // how literal repeats should be
    float urgency = 0.4f;      // shortening intervals, rising contours
    float looseness = 0.3f;    // timing freedom
    float aggression = 0.4f;   // accent contrast + transient snap
    float organicFeel = 0.4f;  // ghosts, graces, drift
    float surprise = 0.3f;     // permission to break expectation
    float brightness = 0.5f;   // high-role weight, pitch lift
    float resolution = 0.5f;   // how firmly the phrase lands

    static float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

    // Deterministic derivation. Non-linear on purpose: the same macro means
    // different things in different musical situations (section 5 examples).
    static FeelVector derive (const GeneratorSettings& s, juce::uint64 motifSeed)
    {
        const float e = s.energy, d = s.density, r = s.randomness;
        // A stable per-motif tilt so two motifs at identical macros can still
        // feel slightly different - small, bounded, deterministic.
        juce::Random mrng ((juce::int64) (motifSeed ^ 0xFEE1600Dull));
        auto tilt = [&mrng] { return (mrng.nextFloat() - 0.5f) * 0.12f; };

        FeelVector f;
        switch (s.mode)
        {
            case Mode::DROP:
                f.drive = clamp01 (0.7f + 0.3f * e);
                f.tension = clamp01 (0.35f + 0.3f * e);
                f.space = clamp01 (0.25f + 0.25f * e - 0.15f * d); // energy buys pre-impact air
                f.aggression = clamp01 (0.5f + 0.5f * e);
                f.resolution = clamp01 (0.7f + 0.2f * e);
                f.urgency = clamp01 (0.4f + 0.3f * e);
                break;
            case Mode::BREAK:
                // Energy raises tension and brightness while KEEPING space.
                f.drive = clamp01 (0.2f + 0.15f * e);
                f.tension = clamp01 (0.3f + 0.55f * e);
                f.space = clamp01 (0.7f - 0.15f * d);
                f.brightness = clamp01 (0.4f + 0.4f * e);
                f.resolution = clamp01 (0.35f + 0.2f * e);
                f.aggression = clamp01 (0.2f + 0.25f * e);
                break;
            case Mode::BUILD:
                f.drive = clamp01 (0.45f + 0.35f * e);
                f.tension = clamp01 (0.55f + 0.45f * e);
                f.space = clamp01 (0.3f - 0.15f * d);
                f.urgency = clamp01 (0.55f + 0.45f * e);
                f.brightness = clamp01 (0.5f + 0.35f * e);
                f.resolution = clamp01 (0.6f + 0.3f * e);   // the destination
                break;
            case Mode::FILL:   // one accelerating bar, everything forward
                f.drive = clamp01 (0.6f + 0.35f * e);
                f.tension = clamp01 (0.5f + 0.4f * e);
                f.space = clamp01 (0.2f - 0.1f * d);
                f.repetition = clamp01 (0.3f - 0.2f * r);
                break;
            case Mode::GROOVE:
                f.drive = clamp01 (0.55f + 0.25f * e);
                f.tension = clamp01 (0.25f + 0.2f * e);
                f.space = clamp01 (0.4f - 0.15f * d);
                f.repetition = clamp01 (0.7f - 0.3f * r);
                f.organicFeel = clamp01 (0.5f + 0.3f * r);
                break;
        }

        // Family colour on top of the section base.
        switch (s.family)
        {
            case Family::PSYTRANCE:
                // Randomness must NOT loosen the core engine: it buys
                // surprise in secondary roles, never looseness.
                f.looseness = 0.05f;
                f.syncopation = clamp01 (0.25f + 0.1f * d);
                f.surprise = clamp01 (0.2f + 0.5f * r);
                f.repetition = clamp01 (0.8f - 0.2f * r);
                break;
            case Family::AFRO:
                // Density strengthens interlock, not subdivision-filling.
                f.syncopation = clamp01 (0.55f + 0.25f * d);
                f.organicFeel = clamp01 (0.55f + 0.25f * r);
                f.looseness = clamp01 (0.3f + 0.2f * r);
                break;
            case Family::BREAKS:
                f.organicFeel = clamp01 (0.6f + 0.3f * r);
                f.looseness = clamp01 (0.35f + 0.3f * r);
                f.syncopation = clamp01 (0.5f + 0.2f * d);
                break;
            case Family::CINEMATIC:
                f.space = clamp01 (f.space + 0.25f);
                f.aggression = clamp01 (f.aggression + 0.15f);
                f.repetition = clamp01 (f.repetition - 0.15f);
                break;
            case Family::ARABIC:
            case Family::MEDITERRANEAN:
                f.organicFeel = clamp01 (0.5f + 0.25f * r);
                f.syncopation = clamp01 (0.45f + 0.15f * d);
                break;
            case Family::EDM:
            case Family::MELODIC_TECHNO:
            case Family::URBAN:
            case Family::HYBRID:
                f.syncopation = clamp01 (0.35f + 0.25f * d);
                f.looseness = clamp01 (0.15f + 0.25f * r);
                break;
        }

        f.surprise = clamp01 (f.surprise + 0.4f * r + tilt());
        f.tension = clamp01 (f.tension + tilt());
        f.brightness = clamp01 (f.brightness + tilt());
        return f;
    }
};

// Per-segment curves of the dimensions a phrase actually moves through.
// Segments are bars for 2/4-bar loops and beats for a 1-bar loop, so a
// one-bar loop still has a miniature statement, development and return.
struct FeelTrajectory
{
    static constexpr int maxSegments = 4;
    int segments = 4;
    std::array<float, maxSegments> density {};    // ornament/ghost budget scale
    std::array<float, maxSegments> tensionArc {}; // velocity tilt
    std::array<float, maxSegments> space {};      // 1 = keep this segment airy
    std::array<float, maxSegments> brightness {}; // lead-role weight

    float at (const std::array<float, maxSegments>& c, int seg) const
    {
        return c[(size_t) juce::jlimit (0, segments - 1, seg)];
    }

    static FeelTrajectory derive (Mode mode, int bars, const FeelVector& f)
    {
        FeelTrajectory t;
        t.segments = juce::jlimit (2, maxSegments, bars > 1 ? bars : 4);
        for (int i = 0; i < t.segments; ++i)
        {
            const float x = t.segments > 1
                ? (float) i / (float) (t.segments - 1) : 1.0f;
            switch (mode)
            {
                case Mode::DROP:   // impact, stable lock, lift, turnaround
                    t.density[(size_t) i] = 1.0f + 0.15f * x;
                    t.tensionArc[(size_t) i] = f.tension * (0.8f + 0.4f * x);
                    t.space[(size_t) i] = i == 0 ? 0.0f : 0.15f;
                    t.brightness[(size_t) i] = f.brightness * (0.9f + 0.2f * x);
                    break;
                case Mode::BREAK:  // space, statement, delayed reply, breath
                    t.density[(size_t) i] = i == t.segments - 1 ? 0.5f : 0.85f + 0.15f * x;
                    t.tensionArc[(size_t) i] = f.tension * (0.6f + 0.5f * x);
                    t.space[(size_t) i] = i == t.segments - 1 ? 1.0f
                                        : i == 0 ? 0.7f : 0.4f;
                    t.brightness[(size_t) i] = f.brightness;
                    break;
                case Mode::BUILD:  // rise everything toward the destination
                    t.density[(size_t) i] = 0.45f + 0.95f * x;
                    t.tensionArc[(size_t) i] = f.tension * (0.5f + 0.6f * x);
                    t.space[(size_t) i] = 0.3f * (1.0f - x);
                    t.brightness[(size_t) i] = f.brightness * (0.7f + 0.5f * x);
                    break;
                case Mode::GROOVE: // stable centre, small waves, turnaround
                    t.density[(size_t) i] = 1.0f + (i == 2 ? 0.15f : 0.0f);
                    t.tensionArc[(size_t) i] = f.tension * (0.9f + (i == 2 ? 0.25f : 0.0f));
                    t.space[(size_t) i] = 0.25f;
                    t.brightness[(size_t) i] = f.brightness;
                    break;
                case Mode::FILL:   // straight ramp into the next downbeat
                    t.density[(size_t) i] = 0.6f + 0.6f * x;
                    t.tensionArc[(size_t) i] = f.tension * (0.4f + 0.8f * x);
                    t.space[(size_t) i] = 0.15f * (1.0f - x);
                    t.brightness[(size_t) i] = f.brightness * (0.7f + 0.4f * x);
                    break;
            }
        }
        return t;
    }
};

} // namespace orcha
