#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace orcha
{

// The sample cutting room: opens over the grid from a sample card's
// waveform. The RAW waveform is shown large; the user drags the two amber
// edge handles to choose the kept region, and the two small triangles at the
// top set light fades from each edge. Every released gesture re-applies the
// transform, so the cards and loops follow live.
class SampleEditPanel : public juce::Component
{
public:
    explicit SampleEditPanel (OrchaAudioProcessor& p);

    void openFor (int slotIndex);
    void close();
    void refreshFromModel();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Handle { none, start, end, fadeIn, fadeOut };

    juce::Rectangle<float> waveRect() const;
    float xForFrac (float f) const;
    float fracForX (float x) const;
    void apply();

    OrchaAudioProcessor& processor;
    int slot = -1;
    SampleTransform::Settings working;
    Handle dragging = Handle::none;

    juce::TextButton resetButton { "RESET" }, closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleEditPanel)
};

} // namespace orcha
