#include "GenerationStrip.h"

namespace orcha
{

GenerationStrip::GenerationStrip()
{
    wireToggleGroup ({ &dropButton, &breakButton, &buildButton, &grooveButton }, 101);
    wireToggleGroup ({ &edmChip, &arabicChip, &medChip, &afroChip, &hybridChip }, 102);
    wireToggleGroup ({ &bars1, &bars2, &bars4 }, 103);
    dropButton.setToggleState (true, juce::dontSendNotification);
    edmChip.setToggleState (true, juce::dontSendNotification);
    bars1.setToggleState (true, juce::dontSendNotification);

    for (auto* chip : { &edmChip, &arabicChip, &medChip, &afroChip, &hybridChip })
        chip->setColour (juce::TextButton::buttonOnColourId, theme::turquoise);
    for (auto* b : { &bars1, &bars2, &bars4 })
        b->setColour (juce::TextButton::buttonOnColourId, theme::turquoise);

    auto initKnob = [this] (juce::Slider& knob, const juce::String& name)
    {
        knob.setName (name);
        knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        knob.setRange (0.0, 1.0);
        knob.onValueChange = [this] { changed(); };
        addAndMakeVisible (knob);
    };
    initKnob (energyKnob, "ENERGY");
    initKnob (densityKnob, "DENSITY");
    initKnob (randomnessKnob, "RANDOMNESS");
    energyKnob.setValue (0.6, juce::dontSendNotification);
    densityKnob.setValue (0.5, juce::dontSendNotification);
    randomnessKnob.setValue (0.3, juce::dontSendNotification);

    generateButton.setColour (juce::TextButton::buttonColourId, theme::amber);
    generateButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    generateButton.onClick = [this] { if (onGenerate) onGenerate(); };
    addAndMakeVisible (generateButton);
}

void GenerationStrip::wireToggleGroup (std::vector<juce::TextButton*> group, int radioId)
{
    for (auto* b : group)
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (radioId);
        b->onClick = [this] { changed(); };
        addAndMakeVisible (*b);
    }
}

void GenerationStrip::setSettings (const GeneratorSettings& s)
{
    juce::TextButton* modes[] = { &dropButton, &breakButton, &buildButton, &grooveButton };
    modes[(int) s.mode]->setToggleState (true, juce::dontSendNotification);
    juce::TextButton* families[] = { &edmChip, &arabicChip, &medChip, &afroChip, &hybridChip };
    families[(int) s.family]->setToggleState (true, juce::dontSendNotification);
    energyKnob.setValue (s.energy, juce::dontSendNotification);
    densityKnob.setValue (s.density, juce::dontSendNotification);
    randomnessKnob.setValue (s.randomness, juce::dontSendNotification);
    (s.bars >= 4 ? bars4 : s.bars >= 2 ? bars2 : bars1)
        .setToggleState (true, juce::dontSendNotification);
}

GeneratorSettings GenerationStrip::getSettings() const
{
    GeneratorSettings s;
    s.mode = breakButton.getToggleState() ? Mode::BREAK
           : buildButton.getToggleState() ? Mode::BUILD
           : grooveButton.getToggleState() ? Mode::GROOVE : Mode::DROP;
    s.family = arabicChip.getToggleState() ? Family::ARABIC
             : medChip.getToggleState() ? Family::MEDITERRANEAN
             : afroChip.getToggleState() ? Family::AFRO
             : hybridChip.getToggleState() ? Family::HYBRID : Family::EDM;
    s.energy = (float) energyKnob.getValue();
    s.density = (float) densityKnob.getValue();
    s.randomness = (float) randomnessKnob.getValue();
    s.bars = bars4.getToggleState() ? 4 : bars2.getToggleState() ? 2 : 1;
    return s;
}

void GenerationStrip::setGenerateEnabled (bool enabled, bool busy)
{
    generateButton.setEnabled (enabled);
    generateButton.setButtonText (busy ? "GENERATING..." : "GENERATE LOOPS");
    generateButton.setColour (juce::TextButton::buttonColourId,
                              enabled ? theme::amber : theme::panelLight);
    generateButton.setColour (juce::TextButton::textColourOffId,
                              enabled ? juce::Colours::black : theme::textDim);
}

void GenerationStrip::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Knob captions.
    auto knobArea = [] (const juce::Slider& s) { return s.getBounds(); };
    g.setColour (theme::textDim);
    g.setFont (theme::label (10.0f));
    for (const auto* knob : { &energyKnob, &densityKnob, &randomnessKnob })
        g.drawText (knob->getName(),
                    knobArea (*knob).withHeight (12).translated (0, -12).expanded (14, 0),
                    juce::Justification::centred);
}

void GenerationStrip::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    auto modeArea = area.removeFromLeft (juce::jmax (230, area.getWidth() * 21 / 100));
    const int modeW = modeArea.getWidth() / 4;
    for (auto* b : { &dropButton, &breakButton, &buildButton, &grooveButton })
        b->setBounds (modeArea.removeFromLeft (modeW).reduced (2, 4));

    // Family chips get widths proportional to their label, so MEDITERRANEAN
    // is readable while EDM stays compact.
    area.removeFromLeft (6);
    auto chipArea = area.removeFromLeft (juce::jmax (330, area.getWidth() * 40 / 100));
    juce::TextButton* chips[] = { &edmChip, &arabicChip, &medChip, &afroChip, &hybridChip };
    int units = 0;
    for (auto* c : chips)
        units += c->getButtonText().length() + 4;
    int consumed = 0;
    for (auto* c : chips)
    {
        const int share = (c->getButtonText().length() + 4) * chipArea.getWidth() / units;
        c->setBounds (chipArea.getX() + consumed, chipArea.getY() + 6,
                      share - 3, chipArea.getHeight() - 12);
        consumed += share;
    }

    auto generateArea = area.removeFromRight (juce::jmax (160, area.getWidth() * 26 / 100));
    generateButton.setBounds (generateArea.reduced (4, 2));

    auto barsArea = area.removeFromRight (168);
    const int barW = barsArea.getWidth() / 3;
    for (auto* b : { &bars1, &bars2, &bars4 })
        b->setBounds (barsArea.removeFromLeft (barW).reduced (2, 8));

    // Knobs take what is left, captions drawn above them in paint().
    area.removeFromTop (12);
    const int knobW = area.getWidth() / 3;
    energyKnob.setBounds (area.removeFromLeft (knobW).reduced (2));
    densityKnob.setBounds (area.removeFromLeft (knobW).reduced (2));
    randomnessKnob.setBounds (area.reduced (2));
}

} // namespace orcha
