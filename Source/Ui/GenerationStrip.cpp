#include "GenerationStrip.h"

namespace orcha
{

GenerationStrip::GenerationStrip()
{
    wireToggleGroup ({ &dropButton, &breakButton, &buildButton, &grooveButton, &fillButton }, 101);
    wireToggleGroup ({ &edmChip, &melodicChip, &psyChip, &urbanChip, &breaksChip,
                       &arabicChip, &medChip, &afroChip, &cinematicChip, &hybridChip }, 102);
    wireToggleGroup ({ &bars1, &bars2, &bars4 }, 103);
    dropButton.setToggleState (true, juce::dontSendNotification);
    edmChip.setToggleState (true, juce::dontSendNotification);
    bars1.setToggleState (true, juce::dontSendNotification);

    for (auto* chip : { &edmChip, &melodicChip, &psyChip, &urbanChip, &breaksChip,
                        &arabicChip, &medChip, &afroChip, &cinematicChip, &hybridChip })
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
    initKnob (randomnessKnob, "RANDOM"); // caption; fits the narrow knob column
    energyKnob.setValue (0.6, juce::dontSendNotification);
    densityKnob.setValue (0.5, juce::dontSendNotification);
    randomnessKnob.setValue (0.3, juce::dontSendNotification);

    // The global pitch switch: on by default; off renders every loop without
    // pitch moves (countdowns, rising rolls, random detunes).
    pitchButton.setClickingTogglesState (true);
    pitchButton.setToggleState (true, juce::dontSendNotification);
    pitchButton.setColour (juce::TextButton::buttonOnColourId, theme::amber);
    pitchButton.onClick = [this]
    {
        if (onPitchToggle)
            onPitchToggle (pitchButton.getToggleState());
    };
    addAndMakeVisible (pitchButton);

    generateButton.setColour (juce::TextButton::buttonColourId, theme::amber);
    generateButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    generateButton.onClick = [this] { if (onGenerate) onGenerate(); };
    addAndMakeVisible (generateButton);

    // GENERATE SET: one musical world as groove / build / drop / break.
    setButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    setButton.setColour (juce::TextButton::textColourOffId, theme::turquoise);
    setButton.setTooltip ("Generate a related SET: 3x GROOVE, 3x BUILD, 3x DROP, 3x BREAK - one musical world");
    setButton.onClick = [this] { if (onGenerateSet) onGenerateSet(); };
    addAndMakeVisible (setButton);
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
    juce::TextButton* modes[] = { &dropButton, &breakButton, &buildButton,
                                  &grooveButton, &fillButton };
    modes[juce::jlimit (0, 4, (int) s.mode)]->setToggleState (true, juce::dontSendNotification);
    // Indexed by the Family enum order, not the visual order.
    juce::TextButton* families[] = { &edmChip, &melodicChip, &psyChip, &arabicChip,
                                     &medChip, &afroChip, &cinematicChip, &hybridChip,
                                     &urbanChip, &breaksChip };
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
           : grooveButton.getToggleState() ? Mode::GROOVE
           : fillButton.getToggleState() ? Mode::FILL : Mode::DROP;
    s.family = melodicChip.getToggleState() ? Family::MELODIC_TECHNO
             : psyChip.getToggleState() ? Family::PSYTRANCE
             : urbanChip.getToggleState() ? Family::URBAN
             : breaksChip.getToggleState() ? Family::BREAKS
             : arabicChip.getToggleState() ? Family::ARABIC
             : medChip.getToggleState() ? Family::MEDITERRANEAN
             : afroChip.getToggleState() ? Family::AFRO
             : cinematicChip.getToggleState() ? Family::CINEMATIC
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

    // Knob captions. The knobs sit flush against each other, so the caption
    // boxes may lean a few pixels into the neighbouring column - but no more,
    // or the texts themselves start overwriting each other ("DENSIRXNDOMNESS").
    auto knobArea = [] (const juce::Slider& s) { return s.getBounds(); };
    g.setColour (theme::textDim);
    g.setFont (theme::label (9.0f));
    for (const auto* knob : { &energyKnob, &densityKnob, &randomnessKnob })
        g.drawText (knob->getName(),
                    knobArea (*knob).withHeight (12).translated (0, -12).expanded (8, 0),
                    juce::Justification::centred);
}

void GenerationStrip::resized()
{
    auto area = getLocalBounds().reduced (10, 8);

    auto modeArea = area.removeFromLeft (juce::jmax (275, area.getWidth() * 25 / 100));
    const int modeW = modeArea.getWidth() / 5;
    for (auto* b : { &dropButton, &breakButton, &buildButton, &grooveButton, &fillButton })
        b->setBounds (modeArea.removeFromLeft (modeW).reduced (2, 4));

    // Eight family chips in two rows of four, widths proportional to their
    // labels so MEDITERRANEAN reads while EDM stays compact.
    area.removeFromLeft (6);
    auto chipArea = area.removeFromLeft (juce::jmax (330, area.getWidth() * 40 / 100));
    juce::TextButton* chipRows[2][5] = {
        { &edmChip, &melodicChip, &psyChip, &urbanChip, &breaksChip },
        { &arabicChip, &medChip, &afroChip, &cinematicChip, &hybridChip } };
    const int rowH = chipArea.getHeight() / 2;
    for (int row = 0; row < 2; ++row)
    {
        int units = 0;
        for (auto* c : chipRows[row])
            units += c->getButtonText().length() + 4;
        int consumed = 0;
        for (auto* c : chipRows[row])
        {
            const int share = (c->getButtonText().length() + 4) * chipArea.getWidth() / units;
            c->setBounds (chipArea.getX() + consumed, chipArea.getY() + row * rowH + 2,
                          share - 3, rowH - 4);
            consumed += share;
        }
    }

    auto generateArea = area.removeFromRight (juce::jmax (160, area.getWidth() * 26 / 100));
    setButton.setBounds (generateArea.removeFromRight (46).reduced (2, 8));
    generateButton.setBounds (generateArea.reduced (4, 2));

    pitchButton.setBounds (area.removeFromRight (56).reduced (2, 8));
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
