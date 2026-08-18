#include "Theme.h"

namespace orcha::theme
{

juce::Font heading (float height)
{
    return juce::Font (juce::FontOptions (height, juce::Font::bold));
}

juce::Font label (float height)
{
    return juce::Font (juce::FontOptions (height));
}

void neonRect (juce::Graphics& g, juce::Rectangle<float> r, float corner,
               juce::Colour colour, float strength)
{
    // Widening strokes with falling alpha read as bloom on a dark ground.
    // Four layers, wider and hotter than a hint: this is the neon the user
    // asked for, not a suggestion of it.
    const float widths[4] = { 14.0f, 9.0f, 5.0f, 2.5f };
    const float alphas[4] = { 0.10f, 0.18f, 0.32f, 0.55f };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (colour.withAlpha (juce::jmin (1.0f, alphas[i] * strength)));
        g.drawRoundedRectangle (r.expanded (widths[i] * 0.35f), corner + widths[i] * 0.3f,
                                widths[i]);
    }
    g.setColour (colour.brighter (0.25f).withAlpha (juce::jmin (1.0f, 0.95f * strength)));
    g.drawRoundedRectangle (r, corner, 1.6f);
}

void neonPath (juce::Graphics& g, const juce::Path& path, juce::Colour colour,
               float strength)
{
    const float widths[3] = { 10.0f, 5.5f, 2.5f };
    const float alphas[3] = { 0.16f, 0.30f, 0.55f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (colour.withAlpha (juce::jmin (1.0f, alphas[i] * strength)));
        g.strokePath (path, juce::PathStrokeType (widths[i],
                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (colour.withAlpha (juce::jmin (1.0f, 0.95f * strength)));
    g.strokePath (path, juce::PathStrokeType (1.2f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void paintWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                    const juce::AudioBuffer<float>& buffer, juce::Colour colour)
{
    const int numSamples = buffer.getNumSamples();
    const int columns = juce::jmax (1, (int) area.getWidth());
    if (numSamples == 0 || columns <= 0)
        return;

    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;
    const int chans = buffer.getNumChannels();

    std::vector<float> peaks ((size_t) columns, 0.0f);
    for (int x = 0; x < columns; ++x)
    {
        const int start = (int) ((juce::int64) x * numSamples / columns);
        const int end = (int) ((juce::int64) (x + 1) * numSamples / columns);
        float peak = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int i = start; i < juce::jmax (start + 1, end); ++i)
                peak = juce::jmax (peak, std::abs (d[juce::jmin (i, numSamples - 1)]));
        }
        peaks[(size_t) x] = peak;
    }

    // Halo pass first - a wide translucent copy behind the bars turns each
    // waveform into a small neon sign - then the crisp core.
    // Two halo passes - a wide soft one and a tighter hotter one - then the
    // crisp core. The wave becomes a lit tube, not a bar chart.
    g.setColour (colour.withAlpha (0.16f));
    for (int x = 0; x < columns; ++x)
    {
        const float h = juce::jmax (1.0f, peaks[(size_t) x] * halfH) + 5.0f;
        g.fillRect (area.getX() + (float) x - 2.5f, midY - h, 6.0f, h * 2.0f);
    }
    g.setColour (colour.withAlpha (0.38f));
    for (int x = 0; x < columns; ++x)
    {
        const float h = juce::jmax (1.0f, peaks[(size_t) x] * halfH) + 2.0f;
        g.fillRect (area.getX() + (float) x - 1.0f, midY - h, 3.0f, h * 2.0f);
    }
    g.setColour (colour.brighter (0.15f));
    for (int x = 0; x < columns; ++x)
    {
        const float h = juce::jmax (1.0f, peaks[(size_t) x] * halfH);
        g.fillRect (area.getX() + (float) x, midY - h, 1.0f, h * 2.0f);
    }
}

OrchaLookAndFeel::OrchaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, amber);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha (0.3f));
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::textColourId, textDim);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, textDim);
}

void OrchaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    auto area = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    auto knob = area.withSizeKeepingCentre (size, size);
    const float radius = size * 0.5f;
    const auto centre = knob.getCentre();

    g.setColour (juce::Colour (0xff0c0e10));
    g.fillEllipse (knob);
    g.setColour (outline);
    g.drawEllipse (knob.reduced (0.5f), 1.0f);

    // Track + value arc.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f,
                         0.0f, startAngle, endAngle, true);
    g.setColour (panelLight);
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f,
                         0.0f, startAngle, angle, true);
    // The value arc is the knob's neon tube: bloom first, crisp core on top.
    g.setColour (amber.withAlpha (0.22f));
    g.strokePath (value, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (amber.withAlpha (0.45f));
    g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (amberBright);
    g.strokePath (value, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Pointer.
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -radius + 4.0f, 3.0f, radius * 0.45f, 1.5f);
    g.setColour (amberBright);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void OrchaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;
    if (down)
        colour = colour.brighter (0.2f);
    else if (highlighted)
        colour = colour.brighter (0.08f);
    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 6.0f);

    // Anything accent-lit glows: toggled chips, the amber GENERATE, the SET
    // teal. Dark idle buttons keep the plain outline - restraint is what
    // makes the lit ones read as neon.
    const bool lit = button.getToggleState()
                  || colour.getPerceivedBrightness() > 0.45f;
    if (lit)
        neonRect (g, bounds.reduced (1.0f), 5.0f,
                  colour.getPerceivedBrightness() > 0.45f ? colour
                                                          : amberBright,
                  highlighted || down ? 1.15f : 0.95f);
    else
    {
        g.setColour (highlighted ? textDim : outline);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }
}

juce::Font OrchaLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    // Long labels (MEDITERRANEAN) shrink to their button instead of truncating.
    const float byWidth = 1.5f * (float) button.getWidth()
                        / (float) juce::jmax (1, button.getButtonText().length());
    return heading (juce::jmin (15.0f, (float) buttonHeight * 0.45f, byWidth));
}

} // namespace orcha::theme
