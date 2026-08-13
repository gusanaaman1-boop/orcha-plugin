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
            const bool isNewLoop = incoming != nullptr
                && (current == nullptr || incoming->optionIndex != current->optionIndex);
            outgoing = std::move (current);        // audio thread never frees
            current = std::move (incoming);
            incoming = nullptr;
            hasIncoming = false;
            phase = 0.0;

            // While the host plays, a newly chosen loop waits for the next bar
            // line instead of stumbling in mid-bar. Re-renders of the loop
            // already playing keep going seamlessly; starting from silence or
            // a stopped host is immediate.
            if (isNewLoop && hostPlaying && hostPpq >= 0.0)
            {
                const double nextBar = std::ceil (hostPpq / 4.0) * 4.0;
                startAtPpq = nextBar - hostPpq < 0.02 ? -1.0 : nextBar;
            }
            else
                startAtPpq = -1.0;
        }
    }

    if (current == nullptr || current->buffer.getNumSamples() == 0)
    {
        playingIndex.store (-1, std::memory_order_relaxed);
        return;
    }
    playingIndex.store (current->optionIndex, std::memory_order_relaxed);

    // Holding for the bar line. A stopped host cancels the wait.
    if (startAtPpq >= 0.0)
    {
        if (hostPlaying && hostPpq >= 0.0 && hostPpq < startAtPpq)
            return;
        startAtPpq = -1.0;
    }

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
    fraction.store ((float) (phase / (double) loopLen), std::memory_order_relaxed);
    juce::ignoreUnused (sampleRate);
}

} // namespace orcha
