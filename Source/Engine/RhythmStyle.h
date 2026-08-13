#pragma once

#include "Pattern.h"
#include <vector>

namespace orcha
{

// A skeleton is one bar (16 steps) of protected structural hits plus the
// style's preferences. Skeletons are constraint systems, not loops: the
// generator ornaments, thins and mutates around them, but at low randomness
// the skeleton's identity must survive.
struct Skeleton
{
    const char* name;
    // step -> role placed there. Multiple entries may share a step.
    struct Hit { int step; Role role; float accent; }; // accent 0..1
    std::vector<Hit> hits;
    double defaultSwing = 0.0;      // 0..1 of half a 16th on odd steps
    // Steps (mod 16) where this style likes ornamental/secondary events.
    std::vector<int> ornamentSteps;
};

// Everything the generator needs to know about one rhythm family.
struct StyleInfo
{
    std::vector<Skeleton> skeletons;
    float ornamentDensity = 0.5f;   // how busy the style is between anchors
    bool  fourFloorAnchor = false;  // LOW is locked to every beat
    bool  interlocking = false;     // AFRO: secondary events avoid anchor steps

    // The groove layer. accentMap is the cyclic weight of each 16th position -
    // the difference between a grid and a pocket. Feel offsets lay a role
    // back (+ms) or push it (-ms); ghostiness scales the designed ghost-note
    // layer between the written hits.
    std::array<float, 16> accentMap {
        1.0f, 0.8f, 0.9f, 0.8f, 0.95f, 0.8f, 0.9f, 0.8f,
        1.0f, 0.8f, 0.9f, 0.8f, 0.95f, 0.8f, 0.9f, 0.85f };
    float highFeelMs = 0.0f;
    float midFeelMs = 0.0f;
    float ghostiness = 0.5f;
};

namespace RhythmStyle
{
    const StyleInfo& get (Family family);
}

} // namespace orcha
