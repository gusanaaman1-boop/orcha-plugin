#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace orcha
{

// The per-option step editor the main screen deliberately does not have:
// opened from one card's EDIT chip, three lanes (HIGH / MID / LOW), one
// column per 16th.
//
//   click an empty cell    add a hit
//   click a hit            remove it
//   drag a hit vertically  its velocity (bar height follows)
//
// Fractional events (rolls, grace notes) are drawn as thin ticks and kept
// verbatim - the grid edits only whole-step hits. Every change re-renders
// the option immediately; a playing card follows its edit live.
class PatternEditPanel : public juce::Component
{
public:
    explicit PatternEditPanel (OrchaAudioProcessor& p);

    void openFor (int optionIndex);
    void close();
    int editingIndex() const { return index; }

    // Called by the editor whenever the model changes.
    void refreshFromModel();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct CellRef { int lane = -1, step = -1; };   // lane 0=HIGH 1=MID 2=LOW

    static Role laneRole (int lane) { return lane == 0 ? Role::HIGH
                                           : lane == 1 ? Role::MID : Role::LOW; }
    juce::Rectangle<float> gridArea() const;
    CellRef cellAt (juce::Point<float> pos) const;
    // The whole-step event in this cell, or nullptr.
    Event* eventInCell (Pattern& p, CellRef cell) const;
    void applyWorking();

    OrchaAudioProcessor& processor;
    int index = -1;
    Pattern working;                 // the pattern being edited (local copy)
    juce::TextButton resetButton { "RESET" }, closeButton { "CLOSE" };
    // Amount sliders, colour-coded: reverb red, delay blue. 0 = off.
    juce::Slider reverbSlider, delaySlider;

    CellRef pressed;
    bool draggedVelocity = false;
    bool pendingAdd = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditPanel)
};

} // namespace orcha
