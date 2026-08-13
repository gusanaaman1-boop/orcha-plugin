#pragma once

#include "Pattern.h"
#include <vector>

namespace orcha
{

// Engine 2.0 Phase 5: the engine no longer accepts the first valid random
// pattern. A pool of symbolic candidates is scored for musical quality,
// compared through perceptual features, and the 12 cards are selected
// JOINTLY for quality and diversity. Deterministic and inspectable - no
// opaque model, every score explains itself.
namespace MusicalScorer
{
    // Perceptual features: what two patterns must differ in to FEEL
    // different. Velocity-weighted, so ghost-only changes stay small.
    struct Features
    {
        std::array<float, 16> onsetHist {};  // weight per 16th (mod bar)
        float syncopation = 0.0f;            // off-beat share of the weight
        float density = 0.0f;                // events per step
        float silenceRatio = 0.0f;           // empty steps share
        Role lead = Role::MID;               // busiest decorated voice
        std::array<float, 3> roleBalance {}; // LOW/MID/HIGH weight share
        std::array<float, 4> tension {};     // per-segment curve
        bool hasFill = false;
    };

    Features extract (const Pattern& p);

    // Weighted, meter-aware distance in ~[0,1].
    float distance (const Features& a, const Features& b);

    struct ScoreBreakdown
    {
        float pulseClarity = 0.0f;
        float syncopationFit = 0.0f;
        float negativeSpace = 0.0f;
        float densityFit = 0.0f;
        float anchorIntegrity = 0.0f;
        float boundaryQuality = 0.0f;
        float penalties = 0.0f;
        float total = 0.0f;

        juce::String describe() const;   // debug/diagnostic output
    };

    ScoreBreakdown score (const Pattern& p, const GeneratorSettings& s);

    // Joint selection: `count` indices into `pool`, chosen greedily by
    //   selectionScore = quality + diversityWeight * minDistanceToSelected
    //                  + personaAffinity(slot)
    // over candidates above a quality floor (relative to the pool's best).
    // Slots 0-2 direct/anchored, 3-5 driving/motif, 6-8 syncopated/organic,
    // 9-11 dramatic/spacious. Deterministic; stable index tie-break.
    std::vector<int> selectDiverse (const std::vector<Pattern>& pool,
                                    const std::vector<Features>& features,
                                    const std::vector<ScoreBreakdown>& scores,
                                    int count);
}

} // namespace orcha
