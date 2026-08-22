#include "SampleBrowser.h"

namespace orcha
{

static bool isAudioFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff"
        || ext == ".flac" || ext == ".mp3" || ext == ".ogg";
}

juce::File SampleBrowser::rootMemoryFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("ORCHA-sample-folder.txt");
}

SampleBrowser::SampleBrowser()
{
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId,
                    juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    upButton.onClick = [this]
    {
        if (current != root)
        {
            current = current.getParentDirectory();
            refresh();
        }
    };
    folderButton.setTooltip ("Choose the library folder ORCHA always opens");
    folderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Choose your sample library folder", root);
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().isDirectory())
                    setRoot (fc.getResult());
            });
    };
    fileButton.setTooltip ("Load a single file from anywhere");
    fileButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Load a sample", current, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile())
                    pickFile (fc.getResult());
            });
    };
    closeButton.onClick = [this] { close(); };
    for (auto* b : { &upButton, &folderButton, &fileButton, &closeButton })
        addAndMakeVisible (*b);

    // The remembered library, or a sensible home until one is chosen.
    juce::File remembered (rootMemoryFile().loadFileAsString().trim());
    setRoot (remembered.isDirectory()
                 ? remembered
                 : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
             false);
}

void SampleBrowser::setRoot (const juce::File& dir, bool remember)
{
    if (! dir.isDirectory())
        return;
    root = current = dir;
    if (remember)
        rootMemoryFile().replaceWithText (dir.getFullPathName());
    refresh();
}

void SampleBrowser::openFor (int slot)
{
    targetSlot = slot;
    refresh();
    setVisible (true);
    toFront (true);
}

void SampleBrowser::refresh()
{
    entries.clear();
    // Folders first, then audio files, both alphabetical - a library, not
    // a filesystem dump.
    for (const auto& e : juce::RangedDirectoryIterator (
             current, false, "*", juce::File::findDirectories))
        if (! e.getFile().isHidden())
            entries.push_back ({ e.getFile(), true });
    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b)
               { return a.file.getFileName().compareIgnoreCase (b.file.getFileName()) < 0; });
    const size_t dirCount = entries.size();
    for (const auto& e : juce::RangedDirectoryIterator (
             current, false, "*", juce::File::findFiles))
        if (! e.getFile().isHidden() && isAudioFile (e.getFile()))
            entries.push_back ({ e.getFile(), false });
    std::sort (entries.begin() + (long) dirCount, entries.end(),
               [] (const Entry& a, const Entry& b)
               { return a.file.getFileName().compareIgnoreCase (b.file.getFileName()) < 0; });
    list.updateContent();
    list.repaint();
    repaint();
}

bool SampleBrowser::pickFile (const juce::File& f)
{
    if (! f.existsAsFile() || onPick == nullptr)
        return false;
    onPick (targetSlot, f);
    close();
    return true;
}

void SampleBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h,
                                      bool selected)
{
    if (row < 0 || row >= (int) entries.size())
        return;
    const auto& e = entries[(size_t) row];
    if (selected)
    {
        g.setColour (theme::turquoise.withAlpha (0.12f));
        g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
    }
    g.setFont (theme::label (13.0f));
    g.setColour (e.isDir ? theme::turquoise : theme::text);
    const auto name = e.isDir ? e.file.getFileName() + "  /"
                              : e.file.getFileName();
    g.drawText (name, 12, 0, w - 20, h, juce::Justification::centredLeft);
}

void SampleBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= (int) entries.size())
        return;
    const auto e = entries[(size_t) row];
    if (e.isDir)
    {
        current = e.file;
        refresh();
    }
    else
        pickFile (e.file);
}

void SampleBrowser::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    // Fully opaque: whatever room was open underneath must not ghost through.
    g.setColour (theme::background);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (theme::panel.withAlpha (0.5f));
    g.fillRoundedRectangle (bounds.reduced (1.0f), 9.0f);
    theme::neonRect (g, bounds.reduced (1.5f), 9.0f, theme::turquoise, 0.5f);

    g.setColour (theme::text);
    g.setFont (theme::heading (15.0f));
    g.drawText ("SAMPLES", 18, 10, 90, 24, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::label (11.0f));
    // Where we are, relative to the library root.
    auto where = current == root
        ? root.getFileName()
        : root.getFileName() + " / "
              + current.getRelativePathFrom (root).replaceCharacter ('\\\\', '/');
    g.drawText (where, 116, 10, getWidth() - 400, 24,
                juce::Justification::centredLeft);
    g.setFont (theme::label (10.0f));
    g.drawText ("click a file to load it into SAMPLE "
                    + juce::String (targetSlot + 1),
                18, getHeight() - 24, getWidth() - 36, 16,
                juce::Justification::centredLeft);
}

void SampleBrowser::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (28);
    closeButton.setBounds (header.removeFromRight (70).reduced (2));
    fileButton.setBounds (header.removeFromRight (76).reduced (2));
    folderButton.setBounds (header.removeFromRight (84).reduced (2));
    upButton.setBounds (header.removeFromRight (52).reduced (2));
    area.removeFromTop (4);
    area.removeFromBottom (18);
    list.setBounds (area);
}

} // namespace orcha
