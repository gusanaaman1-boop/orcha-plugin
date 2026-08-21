#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace orcha::theme
{

// Approved palette: dark charcoal, warm amber primaries, turquoise secondary,
// off-white text.
const juce::Colour background   { 0xff101315 };
const juce::Colour panel        { 0xff181d20 };
const juce::Colour panelLight   { 0xff21272b };
const juce::Colour outline      { 0xff2e363b };
const juce::Colour amber        { 0xfff5a623 };
const juce::Colour amberBright  { 0xffffc04d };
const juce::Colour turquoise    { 0xff57d9c6 };
const juce::Colour text         { 0xffEDEDE6 };
const juce::Colour textDim      { 0xff8a948f };

// Option-card waveform tints rotate through these, like the mockup.
inline juce::Colour waveColour (int index)
{
    static const juce::Colour c[] = { juce::Colour (0xffe85d4c), juce::Colour (0xfff5a623),
                                      juce::Colour (0xff57d9c6), juce::Colour (0xffe8c547) };
    return c[index % 4];
}

juce::Font heading (float height);
juce::Font label (float height);

// Neon: layered soft strokes - no shaders, cheap enough for paint().
// strength 1.0 is a clear glow; 0.5 a hint.
void neonRect (juce::Graphics&, juce::Rectangle<float> r, float corner,
               juce::Colour colour, float strength = 1.0f);
void neonPath (juce::Graphics&, const juce::Path&, juce::Colour colour,
               float strength = 1.0f);

// Frequency-coloured waveform, the way the modern fill tools paint samples:
// deep brown for sub, lightening through orange toward 100-400 Hz, bright
// yellow for upper mids, pale blue-white for real highs. Column colour comes
// from the zero-crossing rate of that column's samples - cheap, and honest
// enough for a display.
void paintSpectralWaveform (juce::Graphics&, juce::Rectangle<float> area,
                            const juce::AudioBuffer<float>& buffer,
                            double sampleRate);

// Min/max waveform of a mono-folded buffer into the given rectangle.
void paintWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                    const juce::AudioBuffer<float>& buffer, juce::Colour colour);

// Shared look for knobs and buttons.
class OrchaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OrchaLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace orcha::theme
