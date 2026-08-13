#pragma once

#include <juce_core/juce_core.h>
#include "../Model/InputSample.h"
#include <vector>

namespace orcha
{

enum class Mode   { DROP = 0, BREAK, BUILD, GROOVE };
enum class Family { EDM = 0, ARABIC, MEDITERRANEAN, AFRO, HYBRID };

inline const char* modeName (Mode m)
{
    switch (m)
    {
        case Mode::DROP:   return "DROP";
        case Mode::BREAK:  return "BREAK";
        case Mode::BUILD:  return "BUILD";
        case Mode::GROOVE: return "GROOVE";
    }
    return "DROP";
}

inline const char* familyName (Family f)
{
    switch (f)
    {
        case Family::EDM:           return "EDM";
        case Family::ARABIC:        return "ARABIC";
        case Family::MEDITERRANEAN: return "MEDITERRANEAN";
        case Family::AFRO:          return "AFRO";
        case Family::HYBRID:        return "HYBRID";
    }
    return "EDM";
}

// One triggered hit inside a loop. Positions are in 16th-note steps from the
// loop start; fractional positions carry rolls and grace notes.
struct Event
{
    double pos = 0.0;          // in 16th steps, 0 .. stepCount
    Role   role = Role::LOW;
    float  velocity = 1.0f;    // 0..1, mapped to gain by the renderer
    float  microMs = 0.0f;     // humanization offset applied after swing
    int    pitchSemis = 0;     // resampling pitch shift
    bool   reverse = false;
    double gateSteps = 0.0;    // 0 = ring out; otherwise choke after N steps
    bool   protectedAnchor = false; // structural - validator must not remove
    bool   roll = false;       // part of a fill gesture (tests + renderer hints)
};

// User-facing generation controls, captured at GENERATE time.
struct GeneratorSettings
{
    Mode   mode = Mode::DROP;
    Family family = Family::EDM;
    float  energy = 0.6f;      // 0..1
    float  density = 0.5f;     // 0..1
    float  randomness = 0.3f;  // 0..1
    int    bars = 1;           // 1, 2 or 4
};

// Which loaded sample slot serves each role. -1 = role unused. Slots may
// repeat when fewer samples are loaded than roles.
struct RoleMap
{
    int low = -1, mid = -1, high = -1, fx = -1;

    int slotFor (Role r) const
    {
        switch (r)
        {
            case Role::LOW:  return low;
            case Role::MID:  return mid;
            case Role::HIGH: return high;
            case Role::FX:   return fx;
            case Role::AUTO: break;
        }
        return low;
    }
};

// A generated loop, fully determined by (seed, ornamentSeed, settings).
// Two seeds on purpose: `seed` decides the MOTIF (skeleton, lead role,
// anchors - the loop's character) and `ornamentSeed` decides everything
// decorative. Regenerating a card with a fresh ornamentSeed gives a fresh
// take on the same groove instead of a different groove.
struct Pattern
{
    juce::uint64 seed = 0;          // motif seed
    juce::uint64 ornamentSeed = 0;
    GeneratorSettings settings;
    juce::String name;          // "DROP 01"
    double swing = 0.0;         // 0..1 of a 16th, applied to odd steps
    std::vector<Event> events;

    int stepCount() const { return settings.bars * 16; }

    // Stable fingerprint used by tests and the diversity check: quantized
    // positions + roles, ignoring velocity detail.
    juce::String signature() const
    {
        juce::String s;
        for (const auto& e : events)
            s << juce::roundToInt (e.pos * 4.0) << ':' << (int) e.role << ';';
        return s;
    }
};

} // namespace orcha
