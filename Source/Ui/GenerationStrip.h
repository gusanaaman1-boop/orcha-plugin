#pragma once

#include "Theme.h"
#include "../Engine/Pattern.h"

namespace orcha
{

// The single generation strip: section, family, three macros, length, and the
// one primary action.
class GenerationStrip : public juce::Component
{
public:
    GenerationStrip();

    std::function<void()> onSettingsChanged;   // any control moved
    std::function<void()> onGenerate;
    std::function<void()> onGenerateSet;       // B1: four related sections
    std::function<void (bool)> onPitchToggle;  // the global PITCH switch

    void setPitchEnabled (bool on) { pitchButton.setToggleState (on, juce::dontSendNotification); }

    void setSettings (const GeneratorSettings& s);
    GeneratorSettings getSettings() const;
    void setGenerateEnabled (bool enabled, bool busy);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void wireToggleGroup (std::vector<juce::TextButton*> group, int radioId);
    void changed() { if (onSettingsChanged) onSettingsChanged(); }

    juce::TextButton dropButton { "DROP" }, breakButton { "BREAK" },
                     buildButton { "BUILD" }, grooveButton { "GROOVE" },
                     fillButton { "FILL" };
    // Two rows of five chips - ten families and the strip stays one strip.
    juce::TextButton edmChip { "EDM" }, melodicChip { "MELODIC TECHNO" },
                     psyChip { "PSYTRANCE" }, urbanChip { "URBAN" },
                     breaksChip { "BREAKS" }, arabicChip { "ARABIC" },
                     medChip { "MEDITERRANEAN" }, afroChip { "AFRO" },
                     cinematicChip { "CINEMATIC" }, hybridChip { "HYBRID" };
    juce::Slider energyKnob, densityKnob, randomnessKnob;
    juce::TextButton bars1 { "1 BAR" }, bars2 { "2 BARS" }, bars4 { "4 BARS" };
    juce::TextButton pitchButton { "PITCH" };
    juce::TextButton generateButton { "GENERATE LOOPS" };
    juce::TextButton setButton { "SET" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenerationStrip)
};

} // namespace orcha
