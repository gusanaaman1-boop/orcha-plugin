#include "PluginEditor.h"
#include "Engine/SampleLoader.h"
#include "Core/ProductInfo.h"
#include "OrchaVersion.h"

namespace orcha
{

OrchaAudioProcessorEditor::OrchaAudioProcessorEditor (OrchaAudioProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), editPanel (p), samplePanel (p)
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
        card->onTransformChange = [this, i] (SampleTransform::Settings t)
        {
            processor.setTransform (i, t);
        };
        card->onOpenEditor = [this, i]
        {
            editPanel.close();          // one room at a time
            samplePanel.openFor (i);
        };
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
        card->onEdit = [this, i]
        {
            samplePanel.close();        // one room at a time
            editPanel.openFor (i);
        };
        addAndMakeVisible (*card);
        optionCards[(size_t) i] = std::move (card);
    }
    addChildComponent (editPanel);
    addChildComponent (samplePanel);

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
        sampleCards[(size_t) i]->update (sample, loading, processor.getTransform (i));
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
    samplePanel.refreshFromModel();
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

    auto header = getLocalBounds().removeFromTop (44).reduced (16, 4);

    // The NAAMAN mark opens the header, big and next to the product name -
    // the site logo drawn vectorially: a circle holding an N of two uprights
    // and one gold diagonal, with a soft gold glow under the diagonal.
    {
        const float logoSize = 36.0f;
        auto logo = juce::Rectangle<float> ((float) header.getX(),
                                            (float) header.getCentreY() - logoSize * 0.5f,
                                            logoSize, logoSize);
        auto pt = [&logo] (float u, float v)
        {
            return juce::Point<float> (logo.getX() + u / 40.0f * logo.getWidth(),
                                       logo.getY() + v / 40.0f * logo.getHeight());
        };
        const juce::Colour gold (0xffc9a86a);
        g.setColour (theme::panelLight.withAlpha (0.6f));
        g.fillEllipse (logo.reduced (1.0f));
        g.setColour (theme::text.withAlpha (0.35f));
        g.drawEllipse (logo.reduced (1.0f), 1.3f);
        // Glow first, strokes on top.
        g.setColour (gold.withAlpha (0.25f));
        g.drawLine ({ pt (13.2f, 12.6f), pt (26.8f, 27.4f) }, 5.0f);
        g.setColour (theme::text);
        g.drawLine ({ pt (13.2f, 12.6f), pt (13.2f, 27.4f) }, 2.0f);
        g.drawLine ({ pt (26.8f, 12.6f), pt (26.8f, 27.4f) }, 2.0f);
        g.setColour (gold);
        g.drawLine ({ pt (13.2f, 12.6f), pt (26.8f, 27.4f) }, 2.2f);
    }

    g.setColour (theme::text);
    g.setFont (theme::heading (26.0f));
    g.drawText ("ORCHA", header.withTrimmedLeft (46), juce::Justification::centredLeft);

    g.setColour (theme::turquoise);
    g.setFont (theme::label (12.0f));
    g.drawText ("DROP UP TO 3 SAMPLES", header, juce::Justification::centred);

    // Maker + version, quiet, on the right.
    {
        g.setColour (theme::text);
        g.setFont (theme::heading (11.0f));
        g.drawText (productInfo::maker, header.withTrimmedBottom (header.getHeight() / 2 - 2),
                    juce::Justification::bottomRight);
        // The git describe only earns its place when it says more than the
        // version already does (dirty builds, commits past the tag).
        juce::String versionLine = juce::String ("v") + ORCHA_VERSION_STRING;
        if (! juce::String (ORCHA_GIT_DESCRIBE).startsWith (versionLine))
            versionLine << "  " << ORCHA_GIT_DESCRIBE;
        else if (juce::String (ORCHA_GIT_DESCRIBE) != versionLine)
            versionLine = ORCHA_GIT_DESCRIBE;
        g.setColour (theme::textDim);
        g.setFont (theme::label (9.0f));
        g.drawText (versionLine, header.withTrimmedTop (header.getHeight() / 2),
                    juce::Justification::topRight);
    }

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
    samplePanel.setBounds (grid);       // so does the sample cutting room
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
