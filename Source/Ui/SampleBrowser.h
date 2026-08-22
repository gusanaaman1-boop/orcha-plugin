#pragma once

#include "Theme.h"

namespace orcha
{

// The internal sample browser: a fixed library folder, navigable, one click
// to load into the slot it was opened for. Exists because dragging from
// Cubase's MediaBay cannot work reliably - Steinberg hands the drop over as
// a file PROMISE in an internal format, never as a real path, and a VST3
// window only receives real paths. A browser that lives inside ORCHA
// sidesteps the host entirely.
class SampleBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    SampleBrowser();

    // Where files land when clicked.
    std::function<void (int slot, const juce::File&)> onPick;

    void openFor (int slot);
    void close() { setVisible (false); }

    // The persistent library root (side file, shared standalone/plugin).
    static juce::File rootMemoryFile();
    void setRoot (const juce::File& dir, bool remember = true);
    juce::File getRoot() const { return root; }

    // Test hook: behave as if the row for this file was clicked.
    bool pickFile (const juce::File& f);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h,
                           bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    struct Entry { juce::File file; bool isDir = false; };
    std::vector<Entry> entries;

    juce::File root, current;
    int targetSlot = 0;

    juce::ListBox list { "samples", this };
    juce::TextButton upButton { "UP" }, folderButton { "FOLDER" },
                     fileButton { "FILE..." }, closeButton { "CLOSE" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowser)
};

} // namespace orcha
