#pragma once

#include "Theme.h"
#include "../Model/InputSample.h"
#include "../Engine/SampleTransform.h"

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
    std::function<void (SampleTransform::Settings)> onTransformChange;
    std::function<void()> onOpenEditor;   // click on the waveform
    std::function<void()> onBrowse;       // empty card click: the library
    std::function<void()> onSliceAsKit;   // Phase E: split a loop into a kit
    void setKitPossible (bool possible) { kitButton.setVisible (possible); }

    // nullptr = empty slot. loading = a decode job is in flight.
    void update (InputSample::Ptr sample, bool loading,
                 SampleTransform::Settings transform = {});

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    void openChooser();

    juce::Rectangle<float> waveArea() const;

    int slot;
    InputSample::Ptr sample;
    bool loading = false;
    bool dragOver = false;
    SampleTransform::Settings transform;

    juce::TextButton removeButton { "X" };
    juce::TextButton reverseButton { "REV" }, trimButton { "TRIM" };
    juce::TextButton kitButton { "KIT" };
    juce::ComboBox roleBox;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleCard)
};

} // namespace orcha
