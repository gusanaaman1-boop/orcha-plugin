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
// THE single source of truth for candidate-pool sizing (Phase A1). The old
// 72-vs-96 contradiction is resolved here: 96 is the default, expansion is
// deterministic and prefix-stable (the first 96 of a 192 run are identical
// to a 96 run), and no other file may carry its own number.
struct CandidatePoolConfig
{
    static constexpr int initialPoolSize = 96;
    static constexpr int firstExpansionSize = 144;
    static constexpr int finalExpansionSize = 192;
    static constexpr int outputCount = 12;
};

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

    // Phase A2: the explicit three-stage pipeline.
    //
    // 1) HardValidator - absolute musical rejection, never relaxed. These
    //    are conditions the constructive validator cannot fix.
    bool hardReject (const Pattern& p, const GeneratorSettings& s);

    // 2) Absolute quality floor per family x section - calibration table,
    //    versioned. v1 of the table (2026-08-13): conservative values from
    //    the score distribution of the current engine; the blind-listening
    //    rounds recalibrate it. Read the table in MusicalScorer.cpp.
    float absoluteQualityFloor (Family family, Mode mode);

    // Critical sub-score floors - a candidate with a broken pulse cannot be
    // rescued by great syncopation.
    struct CriticalFloors
    {
        float pulseClarity = 0.2f;
        float anchorIntegrity = 0.2f;
        float boundaryQuality = 0.4f;
    };

    // 3) Joint selection: `count` indices into `pool`, chosen greedily by
    //   selectionScore = quality + diversityWeight * minDistanceToSelected
    //                  + personaAffinity(slot)
    // over candidates that pass the hard reject, the ABSOLUTE floor, the
    // critical sub-score floors and a relative in-batch floor (55% of the
    // pool's best). A starved second pass relaxes the SOFT floors only,
    // documented in order - never hard validity. Slots 0-2 direct/anchored,
    // 3-5 driving/motif, 6-8 syncopated/organic, 9-11 dramatic/spacious.
    // Deterministic; stable index tie-break.
    std::vector<int> selectDiverse (const std::vector<Pattern>& pool,
                                    const std::vector<Features>& features,
                                    const std::vector<ScoreBreakdown>& scores,
                                    int count,
                                    const GeneratorSettings& settings);
}

} // namespace orcha
