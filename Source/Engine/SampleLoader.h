#pragma once

#include "../Model/InputSample.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace orcha
{

// Decodes WAV/AIFF/FLAC into an immutable InputSample. Blocking - call it
// from a worker thread, never from the audio or message thread for big files.
namespace SampleLoader
{
    bool isSupported (const juce::File& file);

    // Returns nullptr on unsupported/corrupt files. Stereo is kept; anything
    // wider is folded down to stereo. Analysis is filled in.
    InputSample::Ptr load (const juce::File& file);
}

} // namespace orcha
