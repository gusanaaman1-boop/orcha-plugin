#pragma once

#include "../Model/InputSample.h"

namespace orcha
{

// Non-destructive input-sample edits: the raw decode is kept and a transform
// produces a fresh immutable InputSample, so toggling is always exact.
namespace SampleTransform
{
    struct Settings
    {
        bool reverse = false;
        bool trimTail = false;    // strip leading/trailing silence below -48 dB
        // The kept region, as fractions of the (trimmed) sample, plus light
        // optional fades measured from each edge of that region.
        float start = 0.0f;
        float end = 1.0f;
        float fadeIn = 0.0f;      // 0..0.5 of the region
        float fadeOut = 0.0f;

        bool isCropped() const { return start > 0.001f || end < 0.999f; }
    };

    InputSample::Ptr apply (const InputSample& raw, Settings settings);
}

} // namespace orcha
