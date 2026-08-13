#pragma once

#include "Pattern.h"
#include "PhrasePlanner.h"
#include "FeelVector.h"
#include <vector>

namespace orcha
{

// Silence as a planned musical event (Engine 2.0 Phase 4). Regions are
// decided BEFORE decoration finishes and are protected: ornaments, ghosts
// and high density cannot refill them. Anchors and rolls pass through -
// silence shapes the decoration layer, structure stays structure.
namespace SilencePlanner
{
    enum class Purpose
    {
        PocketHole,      // an empty half-beat inside the groove
        BreathBeforeOne, // air right before the loop restarts
        LeadDropout,     // the lead voice steps away for half a segment
        QuietRegion      // a protected whole-beat of near-silence
    };

    struct Region
    {
        double start = 0.0, end = 0.0;   // in steps
        bool allRoles = true;            // false = leadOnly
        Purpose purpose = Purpose::PocketHole;

        bool contains (double pos) const { return pos >= start && pos < end; }
    };

    // Deterministic from the motif seed: every take of the same groove
    // breathes in the same places. Call/Response segments keep their shapes.
    inline std::vector<Region> plan (Mode mode, Family family, int bars,
                                     const PhrasePlan& phrase,
                                     const FeelVector& feel,
                                     juce::uint64 motifSeed)
    {
        juce::Random rng ((juce::int64) (motifSeed ^ 0x51E7CEull));
        std::vector<Region> regions;
        const int steps = bars * 16;
        auto segRoleAt = [&] (double pos)
        {
            return phrase.roles[(size_t) juce::jlimit (0, phrase.segments() - 1,
                                                       phrase.segmentOf (pos, steps))];
        };

        // Pocket holes: rhythm needs moments of nothing.
        const int holes = juce::jmax (1, bars / 2);
        for (int h = 0; h < holes; ++h)
        {
            const bool wants = rng.nextFloat() < 0.3f + 0.4f * feel.space;
            const double start = 2.0 + 4.0 * rng.nextInt (juce::jmax (1, bars * 4 - 1));
            if (! wants)
                continue;
            const auto role = segRoleAt (start);
            if (role == PhraseRole::Call || role == PhraseRole::Response)
                continue;
            regions.push_back ({ start, start + 2.0, true, Purpose::PocketHole });
        }

        // Breath before the one: the loop inhales before it restarts.
        if ((mode == Mode::GROOVE || mode == Mode::DROP)
            && rng.nextFloat() < 0.2f + 0.3f * feel.space)
            regions.push_back ({ (double) steps - 1.0, (double) steps,
                                 true, Purpose::BreathBeforeOne });

        // Lead dropout: in a Develop segment the lead voice may step aside
        // for its second half - the other voices carry the thought.
        if (mode == Mode::GROOVE && rng.nextFloat() < 0.3f)
            for (int seg = 0; seg < phrase.segments(); ++seg)
                if (phrase.roles[(size_t) seg] == PhraseRole::Develop)
                {
                    const double segLen = phrase.beatLevel ? 4.0 : 16.0;
                    regions.push_back ({ seg * segLen + segLen * 0.5,
                                         (seg + 1) * segLen,
                                         false, Purpose::LeadDropout });
                    break;
                }

        // Protected quiet: cinematic and break own a whole beat of air.
        if ((family == Family::CINEMATIC || mode == Mode::BREAK)
            && rng.nextFloat() < 0.5f)
        {
            const double beat = 4.0 * (1 + rng.nextInt (juce::jmax (1, bars * 4 - 2)));
            regions.push_back ({ beat, beat + 4.0, true, Purpose::QuietRegion });
        }
        return regions;
    }

    inline bool blocked (const std::vector<Region>& regions, double pos,
                         bool isLeadRole)
    {
        for (const auto& r : regions)
            if (r.contains (pos) && (r.allRoles || isLeadRole))
                return true;
        return false;
    }
}

// A coarse, deterministic tension reading of a finished pattern - used by
// tests now and by the Phase 5 scorer next. Off-beat weight and velocity
// raise it; air lowers it.
namespace TensionModel
{
    inline std::vector<float> measure (const Pattern& p)
    {
        const int segments = p.settings.bars > 1 ? p.settings.bars : 4;
        const double segLen = p.settings.bars > 1 ? 16.0 : 4.0;
        std::vector<float> tension ((size_t) segments, 0.0f);
        for (const auto& e : p.events)
        {
            const int seg = juce::jlimit (0, segments - 1, (int) (e.pos / segLen));
            const bool offbeat = std::fmod (e.pos, 4.0) >= 0.01;
            tension[(size_t) seg] += e.velocity * (offbeat ? 1.2f : 0.5f);
        }
        for (auto& t : tension)
            t = juce::jlimit (0.0f, 1.0f, t / (float) segLen * 1.2f);
        return tension;
    }
}

} // namespace orcha
