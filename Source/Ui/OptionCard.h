#pragma once

#include "Theme.h"
#include "../Playback/PreviewPlayer.h"

namespace orcha
{

// One generated loop option: play, name, waveform, favorite, regenerate, and
// the drag handle that carries the WAV into Cubase.
class OptionCard : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit OptionCard (int indexIn);

    std::function<void()> onPlay, onFavorite, onRegenerate;
    // Return the WAV / MIDI file to drag, or an invalid File if not available.
    std::function<juce::File()> getDragFile;
    std::function<juce::File()> getMidiDragFile;

    void update (PreviewPlayer::Loop::Ptr loop, const juce::String& name,
                 bool present, bool ready, bool favorite, bool playing);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> playArea() const;
    juce::Rectangle<float> heartArea() const;
    juce::Rectangle<float> regenArea() const;
    juce::Rectangle<float> dragArea() const;
    juce::Rectangle<float> midiArea() const;

    int index;
    PreviewPlayer::Loop::Ptr loop;
    juce::String name;
    bool present = false, ready = false, favorite = false, playing = false;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionCard)
};

} // namespace orcha
