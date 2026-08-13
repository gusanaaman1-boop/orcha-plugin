#include "SampleCard.h"
#include "../Engine/SampleLoader.h"

namespace orcha
{

SampleCard::SampleCard (int slotIndex) : slot (slotIndex)
{
    addChildComponent (removeButton);
    removeButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    removeButton.onClick = [this] { if (onClear) onClear(); };

    // Quick non-destructive edits: reverse and tail-trim, both exact toggles.
    for (auto* b : { &reverseButton, &trimButton })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, theme::turquoise);
        b->onClick = [this]
        {
            if (onTransformChange)
                onTransformChange (reverseButton.getToggleState(),
                                   trimButton.getToggleState());
        };
        addChildComponent (*b);
    }

    addChildComponent (roleBox);
    int id = 1;
    for (auto role : { Role::AUTO, Role::LOW, Role::MID, Role::HIGH, Role::FX })
        roleBox.addItem (roleName (role), id++);
    roleBox.setSelectedId (1, juce::dontSendNotification);
    roleBox.onChange = [this]
    {
        if (onRoleChange)
            onRoleChange (static_cast<Role> (roleBox.getSelectedId() - 1));
    };
}

void SampleCard::update (InputSample::Ptr s, bool nowLoading,
                         bool reversed, bool trimmed)
{
    // Timer-driven updates only repaint on an actual change.
    if (sample == s && loading == nowLoading && ! dragOver
        && reverseButton.getToggleState() == reversed
        && trimButton.getToggleState() == trimmed)
        return;
    sample = std::move (s);
    loading = nowLoading;
    dragOver = false;
    removeButton.setVisible (sample != nullptr);
    roleBox.setVisible (sample != nullptr);
    reverseButton.setVisible (sample != nullptr);
    trimButton.setVisible (sample != nullptr);
    reverseButton.setToggleState (reversed, juce::dontSendNotification);
    trimButton.setToggleState (trimmed, juce::dontSendNotification);
    if (sample != nullptr)
        roleBox.setSelectedId ((int) sample->userRole + 1, juce::dontSendNotification);
    repaint();
}

void SampleCard::resized()
{
    removeButton.setBounds (getLocalBounds().reduced (8).removeFromTop (20).removeFromRight (20));
    auto bottom = getLocalBounds().reduced (8).removeFromBottom (20);
    roleBox.setBounds (bottom.removeFromRight (74));
    bottom.removeFromRight (6);
    trimButton.setBounds (bottom.removeFromRight (44));
    bottom.removeFromRight (4);
    reverseButton.setBounds (bottom.removeFromRight (40));
}

void SampleCard::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const bool empty = sample == nullptr;

    g.setColour (theme::panel.withAlpha (empty ? 0.6f : 1.0f));
    g.fillRoundedRectangle (bounds, 8.0f);

    // Empty slots invite a drop with a dashed turquoise border.
    if (empty || dragOver)
    {
        g.setColour (dragOver ? theme::turquoise : theme::turquoise.withAlpha (0.45f));
        juce::Path border;
        border.addRoundedRectangle (bounds.reduced (2.0f), 7.0f);
        const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (border, border, dashes, 2);
        g.fillPath (border);
    }
    else
    {
        g.setColour (theme::outline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    }

    auto area = getLocalBounds().reduced (12);
    g.setColour (theme::text);
    g.setFont (theme::heading (13.0f));
    g.drawText ("SAMPLE " + juce::String (slot + 1), area.removeFromTop (18),
                juce::Justification::centredLeft);

    if (loading)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::label (13.0f));
        g.drawText ("Loading...", area, juce::Justification::centred);
        return;
    }

    if (empty)
    {
        g.setColour (theme::turquoise);
        g.setFont (theme::label (13.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x86\x93  DROP SAMPLE HERE"),
                    area.withTrimmedBottom (6), juce::Justification::centred);
        g.setColour (theme::textDim);
        g.setFont (theme::label (11.0f));
        g.drawText ("click to load", area.removeFromBottom (16), juce::Justification::centred);
        return;
    }

    auto info = area.removeFromBottom (34);
    theme::paintWaveform (g, area.reduced (0, 3).toFloat(), sample->buffer, theme::turquoise);

    g.setColour (theme::textDim);
    g.setFont (theme::label (11.5f));
    g.drawText (sample->name, info.removeFromTop (16), juce::Justification::centredLeft);
    // The resolved role tells the user what the analyzer decided.
    if (sample->userRole == Role::AUTO)
        g.drawText ("AUTO -> " + juce::String (roleName (sample->resolvedRole)),
                    info.withTrimmedRight (80), juce::Justification::centredLeft);
}

void SampleCard::mouseUp (const juce::MouseEvent& e)
{
    if (sample == nullptr && ! loading
        && e.mouseWasClicked() && ! e.mods.isPopupMenu())
        openChooser();
}

void SampleCard::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Load sample", juce::File(), "*.wav;*.aiff;*.aif;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file.existsAsFile() && onFileChosen)
                onFileChosen (file);
        });
}

bool SampleCard::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (SampleLoader::isSupported (juce::File (f)))
            return true;
    return false;
}

void SampleCard::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (const auto& f : files)
        if (SampleLoader::isSupported (juce::File (f)))
        {
            if (onFileChosen)
                onFileChosen (juce::File (f));
            break;
        }
    repaint();
}

} // namespace orcha
