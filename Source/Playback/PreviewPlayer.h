#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace orcha
{

// Plays one rendered loop on the audio thread, looping, phase-locked to the
// host transport when it runs. Real-time safe by construction:
//  - the audio thread only ever tryEnter()s a SpinLock; a miss skips the swap
//    for one block, never blocks
//  - buffers are reference-counted; the audio thread parks retired ones in an
//    outgoing slot and the message thread releases them (releaseRetired())
struct PreviewPlayer
{
    struct Loop : juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<Loop>;
        juce::AudioBuffer<float> buffer;
        double bpm = 120.0;
        int bars = 1;
        int optionIndex = -1;
    };

    // Message thread. Passing nullptr stops playback.
    void play (Loop::Ptr loop);
    void stop() { play (nullptr); }

    // Message thread, from a timer: frees buffers the audio thread retired.
    void releaseRetired();

    // Which option is sounding right now (-1 = none). Any thread.
    int playingOption() const { return playingIndex.load (std::memory_order_relaxed); }
    // Where inside the loop playback is, 0..1. Any thread; drives the
    // playhead bar on the playing card.
    float playbackFraction() const { return fraction.load (std::memory_order_relaxed); }

    // Audio thread only.
    void process (juce::AudioBuffer<float>& out, double hostPpq, bool hostPlaying,
                  double sampleRate);

private:
    juce::SpinLock lock;
    Loop::Ptr incoming, outgoing;   // guarded by lock
    bool hasIncoming = false;       // distinguishes "swap to null" from "no news"

    Loop::Ptr current;              // audio thread only
    double phase = 0.0;             // samples into the loop, audio thread only
    double startAtPpq = -1.0;       // wait for this bar line; -1 = play now
    std::atomic<int> playingIndex { -1 };
    std::atomic<float> fraction { 0.0f };
};

} // namespace orcha
