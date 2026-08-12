#include "PreviewPlayer.h"

namespace orcha
{

void PreviewPlayer::play (Loop::Ptr loop)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    incoming = std::move (loop);
    hasIncoming = true;
}

void PreviewPlayer::releaseRetired()
{
    Loop::Ptr trash;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        trash = std::move (outgoing);
        outgoing = nullptr;
    }
    // trash releases here, on the message thread.
}

void PreviewPlayer::process (juce::AudioBuffer<float>& out, double hostPpq,
                             bool hostPlaying, double sampleRate)
{
    // Collect a pending swap if the lock is free; otherwise try again next block.
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (sl.isLocked() && hasIncoming && outgoing == nullptr)
        {
            outgoing = std::move (current);        // audio thread never frees
            current = std::move (incoming);
            incoming = nullptr;
            hasIncoming = false;
            phase = 0.0;
        }
    }

    if (current == nullptr || current->buffer.getNumSamples() == 0)
    {
        playingIndex.store (-1, std::memory_order_relaxed);
        return;
    }
    playingIndex.store (current->optionIndex, std::memory_order_relaxed);

    const auto& loop = current->buffer;
    const int loopLen = loop.getNumSamples();

    // Phase-lock to the host while it plays; free-run while it is stopped.
    if (hostPlaying && hostPpq >= 0.0)
    {
        const double beatsPerLoop = current->bars * 4.0;
        const double loopPpq = std::fmod (hostPpq, beatsPerLoop);
        const double samplesPerBeat = loopLen / beatsPerLoop;
        phase = loopPpq * samplesPerBeat;
    }

    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        int idx = (int) phase;
        if (idx >= loopLen)
        {
            phase -= loopLen;
            idx = (int) phase;
            if (idx >= loopLen)   // degenerate loop shorter than a block
                idx = 0;
        }
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            out.addSample (ch, i, loop.getSample (juce::jmin (ch, loop.getNumChannels() - 1), idx));
        phase += 1.0;
    }
    juce::ignoreUnused (sampleRate);
}

} // namespace orcha
