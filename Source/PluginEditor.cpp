#include "PluginEditor.h"
#include "Engine/SampleLoader.h"
#include "OrchaVersion.h"

namespace orcha
{

OrchaAudioProcessorEditor::OrchaAudioProcessorEditor (OrchaAudioProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), editPanel (p)
{
    setLookAndFeel (&lookAndFeel);

    for (int i = 0; i < OrchaAudioProcessor::numSlots; ++i)
    {
        auto card = std::make_unique<SampleCard> (i);
        card->onFileChosen = [this, i] (const juce::File& f)
        {
            loadStartedAt[(size_t) i] = juce::Time::getMillisecondCounter();
            processor.loadSampleAsync (i, f);
            refresh();
        };
        card->onClear = [this, i] { processor.clearSample (i); };
        card->onRoleChange = [this, i] (Role r) { processor.setUserRole (i, r); };
        addAndMakeVisible (*card);
        sampleCards[(size_t) i] = std::move (card);
    }

    strip.setSettings (processor.settings);
    strip.onSettingsChanged = [this]
    {
        processor.settings = strip.getSettings();
    };
    strip.onGenerate = [this]
    {
        processor.settings = strip.getSettings();
        processor.generateAll();
    };
    addAndMakeVisible (strip);

    for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
    {
        auto card = std::make_unique<OptionCard> (i);
        card->onPlay = [this, i] { processor.togglePlay (i); };
        card->onFavorite = [this, i] { processor.toggleFavorite (i); };
        card->onRegenerate = [this, i] { processor.regenerateOption (i); };
        card->getDragFile = [this, i] { return processor.ensureWavFor (i); };
        card->getMidiDragFile = [this, i] { return processor.ensureMidiFor (i); };
        card->onEdit = [this, i] { editPanel.openFor (i); };
        addAndMakeVisible (*card);
        optionCards[(size_t) i] = std::move (card);
    }
    addChildComponent (editPanel);

    generateMoreButton.onClick = [this]
    {
        processor.settings = strip.getSettings();
        processor.generateAll();
    };
    generateMoreButton.setColour (juce::TextButton::buttonColourId, theme::panel);
    generateMoreButton.setColour (juce::TextButton::textColourOffId, theme::turquoise);
    addAndMakeVisible (generateMoreButton);

    processor.onModelChanged = [this] { refresh(); };
    startTimer (250);

    setResizable (true, true);
    setResizeLimits (940, 600, 1700, 1080);
    setSize (1100, 700);
    refresh();
}

OrchaAudioProcessorEditor::~OrchaAudioProcessorEditor()
{
    processor.onModelChanged = nullptr;
    setLookAndFeel (nullptr);
}

void OrchaAudioProcessorEditor::refresh()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < OrchaAudioProcessor::numSlots; ++i)
    {
        const auto sample = processor.getSample (i);
        bool loading = false;
        if (sample == nullptr && loadStartedAt[(size_t) i] != 0)
        {
            loading = now - loadStartedAt[(size_t) i] < 10000;   // decode window
            if (! loading)
                loadStartedAt[(size_t) i] = 0;
        }
        if (sample != nullptr)
            loadStartedAt[(size_t) i] = 0;
        sampleCards[(size_t) i]->update (sample, loading);
    }

    const int playing = processor.playingOption();
    for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
    {
        const auto& opt = processor.option (i);
        optionCards[(size_t) i]->update (opt.loop,
                                         opt.present ? opt.pattern.name : juce::String(),
                                         opt.present || processor.optionBusy (i),
                                         opt.ready, opt.favorite, playing == i);
    }

    const bool canGenerate = processor.anySampleLoaded();
    strip.setGenerateEnabled (canGenerate && ! processor.isGenerating(),
                              processor.isGenerating());
    generateMoreButton.setEnabled (canGenerate && ! processor.isGenerating());
    editPanel.refreshFromModel();
}

void OrchaAudioProcessorEditor::timerCallback()
{
    // Playback position / playing flag changes do not push notifications;
    // a slow poll keeps the play states honest without repaint storms.
    refresh();
}

void OrchaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().removeFromTop (44).reduced (16, 6);
    g.setColour (theme::text);
    g.setFont (theme::heading (26.0f));
    g.drawText ("ORCHA", header, juce::Justification::centredLeft);

    g.setColour (theme::turquoise);
    g.setFont (theme::label (12.0f));
    g.drawText ("DROP UP TO 3 SAMPLES", header, juce::Justification::centred);

    g.setColour (theme::textDim);
    g.setFont (theme::label (10.0f));
    g.drawText (juce::String ("v") + ORCHA_VERSION_STRING + " " + ORCHA_GIT_DESCRIBE,
                header, juce::Justification::centredRight);

    // LOOP OPTIONS divider between the strip and the grid.
    const int dividerY = strip.getBottom() + 4;
    auto divider = juce::Rectangle<int> (0, dividerY, getWidth(), 20).reduced (16, 0);
    g.setFont (theme::heading (12.0f));
    g.setColour (theme::textDim);
    g.drawText ("LOOP OPTIONS", divider, juce::Justification::centred);
    const int textW = 110;
    g.setColour (theme::outline);
    g.fillRect (divider.getX(), dividerY + 10, divider.getWidth() / 2 - textW / 2, 1);
    g.fillRect (divider.getCentreX() + textW / 2, dividerY + 10,
                divider.getWidth() / 2 - textW / 2, 1);
}

void OrchaAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (44);                       // header (painted)

    auto sampleRow = area.removeFromTop (juce::jmax (140, area.getHeight() * 24 / 100))
                         .reduced (12, 0);
    const int cardW = sampleRow.getWidth() / 3;
    for (auto& card : sampleCards)
        card->setBounds (sampleRow.removeFromLeft (cardW).reduced (5, 0));

    area.removeFromTop (8);
    strip.setBounds (area.removeFromTop (76).reduced (12, 0));
    area.removeFromTop (26);                       // divider (painted)

    auto bottom = area.removeFromBottom (40).reduced (12, 5);
    generateMoreButton.setBounds (bottom);

    auto grid = area.reduced (12, 2);
    editPanel.setBounds (grid);         // the step editor covers the grid
    const int cols = 4, rows = 3;
    const int w = grid.getWidth() / cols, h = grid.getHeight() / rows;
    for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
    {
        const juce::Rectangle<int> cell (grid.getX() + (i % cols) * w,
                                         grid.getY() + (i / cols) * h, w, h);
        optionCards[(size_t) i]->setBounds (cell.reduced (5, 4));
    }
}

bool OrchaAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (SampleLoader::isSupported (juce::File (f)))
            return true;
    return false;
}

void OrchaAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    // Route to the first free slots, one file per slot.
    int slot = 0;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! SampleLoader::isSupported (file))
            continue;
        while (slot < OrchaAudioProcessor::numSlots && processor.getSample (slot) != nullptr)
            ++slot;
        if (slot >= OrchaAudioProcessor::numSlots)
            break;
        loadStartedAt[(size_t) slot] = juce::Time::getMillisecondCounter();
        processor.loadSampleAsync (slot, file);
        ++slot;
    }
    refresh();
}

juce::AudioProcessorEditor* OrchaAudioProcessor::createEditor()
{
    return new OrchaAudioProcessorEditor (*this);
}

} // namespace orcha
