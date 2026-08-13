#pragma once

#include "PluginProcessor.h"
#include "Ui/SampleCard.h"
#include "Ui/GenerationStrip.h"
#include "Ui/OptionCard.h"
#include "Ui/PatternEditPanel.h"
#include "Ui/SampleEditPanel.h"

namespace orcha
{

// Three zones, exactly like the approved mockup: sample cards, generation
// strip, option grid. The editor is the DragAndDropContainer for drag-out.
class OrchaAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::DragAndDropContainer,
                                  public juce::FileDragAndDropTarget,
                                  private juce::Timer
{
public:
    explicit OrchaAudioProcessorEditor (OrchaAudioProcessor&);
    ~OrchaAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Drops anywhere on the window land in the first free sample slot.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // For the deterministic screenshot tool.
    void openEditPanelFor (int index) { editPanel.openFor (index); }
    void openSamplePanelFor (int slot) { samplePanel.openFor (slot); }

private:
    void refresh();
    void timerCallback() override;

    OrchaAudioProcessor& processor;
    theme::OrchaLookAndFeel lookAndFeel;

    std::array<std::unique_ptr<SampleCard>, OrchaAudioProcessor::numSlots> sampleCards;
    GenerationStrip strip;
    std::array<std::unique_ptr<OptionCard>, OrchaAudioProcessor::numOptions> optionCards;
    juce::TextButton generateMoreButton { "GENERATE MORE" };
    juce::TextButton exportAllButton { "EXPORT ALL" };
    std::unique_ptr<juce::FileChooser> exportChooser;
    PatternEditPanel editPanel;
    SampleEditPanel samplePanel;

    std::array<juce::uint32, OrchaAudioProcessor::numSlots> loadStartedAt {};
    int slowTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrchaAudioProcessorEditor)
};

} // namespace orcha
