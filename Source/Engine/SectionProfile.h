#pragma once

#include "Pattern.h"

namespace orcha
{

// How a musical section shapes generation, before the macros scale it.
struct SectionProfile
{
    float baseDensity;      // fraction of candidate steps that get events
    float velocityFloor;    // ghosts sit here
    float velocityContrast; // accent lift above the floor
    float fillChance;       // roll/fill at phrase end
    float silenceChance;    // pre-impact gap / dropped beat variants
    bool  densityRamp;      // BUILD: density+velocity rise across the phrase
    bool  sparse;           // BREAK: thin anchors too, protect breathing room
};

inline SectionProfile sectionProfile (Mode m)
{
    switch (m)
    {
        case Mode::DROP:   return { 0.70f, 0.35f, 0.65f, 0.45f, 0.30f, false, false };
        case Mode::BREAK:  return { 0.32f, 0.20f, 0.35f, 0.10f, 0.55f, false, true };
        case Mode::BUILD:  return { 0.55f, 0.30f, 0.55f, 0.85f, 0.10f, true,  false };
        case Mode::GROOVE: return { 0.55f, 0.30f, 0.45f, 0.20f, 0.15f, false, false };
    }
    return { 0.55f, 0.3f, 0.45f, 0.2f, 0.15f, false, false };
}

} // namespace orcha
