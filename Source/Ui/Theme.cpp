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
    const float widths[4] = { 10.0f, 6.0f, 3.5f, 2.0f };
    const float alphas[4] = { 0.08f, 0.16f, 0.30f, 0.50f };
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

void paintSpectralWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                            const juce::AudioBuffer<float>& buffer,
                            double sampleRate)
{
    const int numSamples = buffer.getNumSamples();
    // Retina awareness: painting 1-px columns on a 2x display throws away
    // half the resolution and reads as SMEAR (user report). Columns follow
    // the PHYSICAL pixel grid; on a 1x surface scale stays 1.
    const float scale = juce::jlimit (1.0f, 3.0f,
        g.getInternalContext().getPhysicalPixelScaleFactor());
    const int columns = juce::jmax (1, (int) (area.getWidth() * scale));
    const float colW = area.getWidth() / (float) columns;
    if (numSamples == 0 || columns <= 0 || sampleRate <= 0.0)
        return;

    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;
    const int chans = buffer.getNumChannels();

    // The colour map everyone already knows, from the DJ decks: BASS is red,
    // MIDS are green, HIGHS are blue, and mixed content blends (kick under a
    // hat reads purple). Measured as real band ENERGY through one-pole
    // filters - not zero-crossing rate, whose noise bias painted a snare
    // body brown. Crossovers ~120 Hz and ~2 kHz.
    const juce::Colour lowC  (0xffff5347);   // bass: red
    const juce::Colour midC  (0xff2ee06e);   // mids: green
    const juce::Colour highC (0xff54c8ff);   // highs: blue

    // Time-resolved like a spectral meter: the story must be visible INSIDE
    // one hit - a kick opens with a bright click, moves through a green
    // body, lands on a red sub tail. Two things make that legible:
    //   - steeper bands: cascaded one-poles (-12 dB/oct) at ~180 Hz and
    //     ~2 kHz, so a 200 Hz body no longer leaks into the sub band and
    //     smears the whole hit red
    //   - finer time grid: energies are gathered on a 4x supersampled grid
    //     and drawn per column from its sharpest cell, so short phases
    //     (the click) keep their own colour instead of averaging away
    const int cells = columns * 4;
    const float aLow  = std::exp ((float) (-juce::MathConstants<double>::twoPi
                                           * 180.0 / sampleRate));
    const float aMid  = std::exp ((float) (-juce::MathConstants<double>::twoPi
                                           * 2000.0 / sampleRate));
    float lp100a = 0.0f, lp100b = 0.0f, lp2ka = 0.0f, lp2kb = 0.0f;

    std::vector<float> peaks ((size_t) columns, 0.0f);
    std::vector<float> cLow ((size_t) cells, 0.0f),
                       cMid ((size_t) cells, 0.0f),
                       cHigh ((size_t) cells, 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        float v = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            v += buffer.getReadPointer (ch)[i];
        v /= (float) chans;

        lp100a = aLow * lp100a + (1.0f - aLow) * v;
        lp100b = aLow * lp100b + (1.0f - aLow) * lp100a;   // -12 dB/oct
        lp2ka  = aMid * lp2ka  + (1.0f - aMid) * v;
        lp2kb  = aMid * lp2kb  + (1.0f - aMid) * lp2ka;
        const float low = lp100b;
        const float mid = lp2kb - lp100b;
        const float high = v - lp2kb;

        const int cell = juce::jmin (cells - 1,
            (int) ((juce::int64) i * cells / numSamples));
        cLow[(size_t) cell]  += low * low;
        cMid[(size_t) cell]  += mid * mid;
        cHigh[(size_t) cell] += high * high;
        const int x = juce::jmin (columns - 1, cell / 4);
        peaks[(size_t) x] = juce::jmax (peaks[(size_t) x], std::abs (v));
    }

    // Peak-hold across cells: a cell shorter than one period of a 60 Hz wave
    // sees the sine's phase, not its energy, and the kick body strobes
    // green/red. Instant attack, ~6-cell decay - onsets keep their own
    // colour, the body reads as one continuous story.
    auto hold = [] (std::vector<float>& c)
    {
        for (size_t i = 1; i < c.size(); ++i)
            c[i] = juce::jmax (c[i], c[i - 1] * 0.82f);
    };
    hold (cLow);
    hold (cMid);
    hold (cHigh);

    std::vector<float> eLow ((size_t) columns, 0.0f),
                       eMid ((size_t) columns, 0.0f),
                       eHigh ((size_t) columns, 0.0f);
    for (int x = 0; x < columns; ++x)
        for (int k = 0; k < 4; ++k)
        {
            const size_t cell = (size_t) juce::jmin (cells - 1, x * 4 + k);
            eLow[(size_t) x]  += cLow[cell];
            eMid[(size_t) x]  += cMid[cell];
            eHigh[(size_t) x] += cHigh[cell];
        }

    for (int x = 0; x < columns; ++x)
    {
        // Perceptual weighting: highs carry far less energy per loudness, so
        // they get a lift or every hat would drown under its own kick bleed.
        // Calibrated against the real XO kit, 10 ms windows: these weights
        // and the 180 Hz crossover make a kick tell its true story - BLUE
        // click, GREEN body, RED sub tail - instead of averaging to one hue.
        const float wl = eLow[(size_t) x];
        const float wm = eMid[(size_t) x] * 1.6f;
        const float wh = eHigh[(size_t) x] * 5.0f;
        const float sum = wl + wm + wh;
        juce::Colour col = midC;
        if (sum > 1.0e-12f)
        {
            const float r = (lowC.getFloatRed()   * wl + midC.getFloatRed()   * wm + highC.getFloatRed()   * wh) / sum;
            const float gr = (lowC.getFloatGreen() * wl + midC.getFloatGreen() * wm + highC.getFloatGreen() * wh) / sum;
            const float b = (lowC.getFloatBlue()  * wl + midC.getFloatBlue()  * wm + highC.getFloatBlue()  * wh) / sum;
            col = juce::Colour::fromFloatRGBA (r, gr, b, 1.0f);
        }
        if (peaks[(size_t) x] < 0.02f)
            col = col.withAlpha (0.35f);   // near-silence stays quiet visually

        const float hgt = juce::jmax (colW, peaks[(size_t) x] * halfH);
        const float px = area.getX() + (float) x * colW;
        // A whisper of halo, one physical pixel each side - glow without mush.
        g.setColour (col.withAlpha (col.getFloatAlpha() * 0.18f));
        g.fillRect (px - colW, midY - hgt - colW, colW * 3.0f, (hgt + colW) * 2.0f);
        g.setColour (col);
        g.fillRect (px, midY - hgt, colW, hgt * 2.0f);
    }
}

void paintWaveform (juce::Graphics& g, juce::Rectangle<float> area,
                    const juce::AudioBuffer<float>& buffer, juce::Colour colour)
{
    const int numSamples = buffer.getNumSamples();
    const float scale = juce::jlimit (1.0f, 3.0f,
        g.getInternalContext().getPhysicalPixelScaleFactor());
    const int columns = juce::jmax (1, (int) (area.getWidth() * scale));
    const float colW = area.getWidth() / (float) columns;
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
    g.setColour (colour.withAlpha (0.18f));
    for (int x = 0; x < columns; ++x)
    {
        const float h = juce::jmax (colW, peaks[(size_t) x] * halfH) + colW * 2.0f;
        g.fillRect (area.getX() + (float) x * colW - colW, midY - h, colW * 3.0f, h * 2.0f);
    }
    g.setColour (colour);
    for (int x = 0; x < columns; ++x)
    {
        const float h = juce::jmax (colW, peaks[(size_t) x] * halfH);
        g.fillRect (area.getX() + (float) x * colW, midY - h, colW, h * 2.0f);
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
                  highlighted || down ? 0.85f : 0.55f);
    else if (highlighted)
    {
        // Hover: a quiet lift, not a glow - the eye should find the button
        // without the whole strip lighting up under the cursor.
        g.setColour (textDim.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }
    else
    {
        g.setColour (outline.withAlpha (0.8f));
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
