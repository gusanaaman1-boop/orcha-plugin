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
    };

    InputSample::Ptr apply (const InputSample& raw, Settings settings);
}

} // namespace orcha
