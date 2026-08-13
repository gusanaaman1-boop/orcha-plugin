#pragma once

#include <juce_core/juce_core.h>
#include "../Model/InputSample.h"
#include <vector>

namespace orcha
{

enum class Mode   { DROP = 0, BREAK, BUILD, GROOVE };
enum class Family { EDM = 0, MELODIC_TECHNO, PSYTRANCE, ARABIC, MEDITERRANEAN,
                    AFRO, CINEMATIC, HYBRID, URBAN, BREAKS };

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
        case Family::EDM:            return "EDM";
        case Family::MELODIC_TECHNO: return "MELODIC TECHNO";
        case Family::PSYTRANCE:      return "PSYTRANCE";
        case Family::ARABIC:         return "ARABIC";
        case Family::MEDITERRANEAN:  return "MEDITERRANEAN";
        case Family::AFRO:           return "AFRO";
        case Family::CINEMATIC:      return "CINEMATIC";
        case Family::HYBRID:         return "HYBRID";
        case Family::URBAN:          return "URBAN";
        case Family::BREAKS:         return "BREAKS";
    }
    return "EDM";
}

inline Family familyFromName (const juce::String& s)
{
    if (s == "MELODIC TECHNO") return Family::MELODIC_TECHNO;
    if (s == "PSYTRANCE")      return Family::PSYTRANCE;
    if (s == "ARABIC")         return Family::ARABIC;
    if (s == "MEDITERRANEAN")  return Family::MEDITERRANEAN;
    if (s == "AFRO")           return Family::AFRO;
    if (s == "CINEMATIC")      return Family::CINEMATIC;
    if (s == "HYBRID")         return Family::HYBRID;
    if (s == "URBAN")          return Family::URBAN;
    if (s == "BREAKS")         return Family::BREAKS;
    return Family::EDM;
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

// What the generator needs to know about the sample serving a role, so the
// SYMBOLIC stage can respect the audio before anything renders: sustained
// samples need spacing, low-heavy ones must not pile up, weak transients
// cannot carry rolls.
struct RoleTraits
{
    bool sustained = false;      // rings long: needs spacing + gates
    bool lowHeavy = false;       // sub-heavy: no dense stacking
    bool brightShort = false;    // supports fast ornamentation
    bool weakTransient = false;  // better as swells than as rolls
};
using TraitsByRole = std::array<RoleTraits, 5>;   // indexed by (int) Role

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

// Where a loop is headed when it ends (Engine 2.0 Phase 7). Internal - no
// UI control; fresh batches assign transition destinations to a few cards
// so the 12 cover returns, throws and stops.
enum class Destination { LoopBack = 0, ToDrop, ToBreak, ToStop };

// A generated loop, fully determined by (seed, ornamentSeed, settings).
// Two seeds on purpose: `seed` decides the MOTIF (skeleton, lead role,
// anchors - the loop's character) and `ornamentSeed` decides everything
// decorative. Regenerating a card with a fresh ornamentSeed gives a fresh
// take on the same groove instead of a different groove.
struct Pattern
{
    juce::uint64 seed = 0;          // motif seed
    juce::uint64 ornamentSeed = 0;
    // Which generator produced this. Old projects restore through the frozen
    // v1 path bit-for-bit; new generations use v2. Never migrates silently.
    int algo = 1;
    Destination destination = Destination::LoopBack;
    GeneratorSettings settings;
    juce::String name;          // "DROP 01"
    double swing = 0.0;         // 0..1 of a 16th, applied to odd steps
    // Per-card polish, baked into the render (and therefore the dragged WAV):
    // reverb and delay amounts (0 = off), chosen in the step editor.
    float fxReverb = 0.0f;
    float fxDelay = 0.0f;
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
