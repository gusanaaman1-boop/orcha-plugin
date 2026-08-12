#pragma once

#include "Theme.h"
#include "../Model/InputSample.h"

namespace orcha
{

// One of the three sample input slots: drop target, load button, waveform,
// role selector, remove.
class SampleCard : public juce::Component,
                   public juce::FileDragAndDropTarget
{
public:
    explicit SampleCard (int slotIndex);

    std::function<void (const juce::File&)> onFileChosen;
    std::function<void()> onClear;
    std::function<void (Role)> onRoleChange;

    // nullptr = empty slot. loading = a decode job is in flight.
    void update (InputSample::Ptr sample, bool loading);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    void openChooser();

    int slot;
    InputSample::Ptr sample;
    bool loading = false;
    bool dragOver = false;

    juce::TextButton removeButton { "X" };
    juce::ComboBox roleBox;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleCard)
};

} // namespace orcha
