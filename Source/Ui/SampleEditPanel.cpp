#include "SampleEditPanel.h"

namespace orcha
{

SampleEditPanel::SampleEditPanel (OrchaAudioProcessor& p) : processor (p)
{
    resetButton.onClick = [this]
    {
        working.start = 0.0f;
        working.end = 1.0f;
        working.fadeIn = 0.0f;
        working.fadeOut = 0.0f;
        apply();
        repaint();
    };
    addAndMakeVisible (resetButton);

    closeButton.setColour (juce::TextButton::buttonColourId, theme::amber);
    closeButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    setVisible (false);
}

void SampleEditPanel::openFor (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= OrchaAudioProcessor::numSlots
        || processor.getRawSample (slotIndex) == nullptr)
        return;
    slot = slotIndex;
    working = processor.getTransform (slotIndex);
    setVisible (true);
    toFront (false);
    repaint();
}

void SampleEditPanel::close()
{
    setVisible (false);
    slot = -1;
}

void SampleEditPanel::refreshFromModel()
{
    if (slot < 0 || ! isVisible())
        return;
    if (processor.getRawSample (slot) == nullptr)
    {
        close();
        return;
    }
    if (dragging == Handle::none)
    {
        working = processor.getTransform (slot);
        repaint();
    }
}

juce::Rectangle<float> SampleEditPanel::waveRect() const
{
    auto b = getLocalBounds().toFloat().reduced (18.0f);
    b.removeFromTop (42.0f);
    b.removeFromBottom (24.0f);
    return b;
}

float SampleEditPanel::xForFrac (float f) const
{
    const auto w = waveRect();
    return w.getX() + w.getWidth() * juce::jlimit (0.0f, 1.0f, f);
}

float SampleEditPanel::fracForX (float x) const
{
    const auto w = waveRect();
    return juce::jlimit (0.0f, 1.0f, (x - w.getX()) / juce::jmax (1.0f, w.getWidth()));
}

void SampleEditPanel::apply()
{
    if (slot >= 0)
        processor.setTransform (slot, working);
}

void SampleEditPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::background.withAlpha (0.98f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (theme::turquoise.withAlpha (0.7f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.5f);

    const auto raw = slot >= 0 ? processor.getRawSample (slot) : nullptr;
    g.setColour (theme::text);
    g.setFont (theme::heading (16.0f));
    g.drawText ("CUT  -  SAMPLE " + juce::String (slot + 1)
                    + (raw != nullptr ? "   (" + raw->name + ")" : juce::String()),
                18, 12, getWidth() - 200, 22, juce::Justification::centredLeft);
    if (raw == nullptr)
        return;

    const auto wave = waveRect();
    g.setColour (theme::panel);
    g.fillRect (wave);
    theme::paintSpectralWaveform (g, wave.reduced (0.0f, 4.0f), raw->buffer,
                                  raw->sourceSampleRate);

    // Discarded regions dim; the kept region stays bright.
    const float xs = xForFrac (working.start);
    const float xe = xForFrac (working.end);
    g.setColour (theme::background.withAlpha (0.72f));
    g.fillRect (juce::Rectangle<float> (wave.getX(), wave.getY(), xs - wave.getX(),
                                        wave.getHeight()));
    g.fillRect (juce::Rectangle<float> (xe, wave.getY(), wave.getRight() - xe,
                                        wave.getHeight()));

    // Fade wedges: diagonal lines from each edge of the kept region.
    g.setColour (theme::amberBright.withAlpha (0.8f));
    if (working.fadeIn > 0.001f)
    {
        const float fx = xForFrac (working.start
                                   + working.fadeIn * (working.end - working.start));
        g.drawLine (xs, wave.getBottom(), fx, wave.getY(), 1.6f);
    }
    if (working.fadeOut > 0.001f)
    {
        const float fx = xForFrac (working.end
                                   - working.fadeOut * (working.end - working.start));
        g.drawLine (xe, wave.getBottom(), fx, wave.getY(), 1.6f);
    }

    // Edge handles: amber verticals with grab tabs.
    for (const float x : { xs, xe })
    {
        g.setColour (theme::amber);
        g.fillRect (x - 1.5f, wave.getY(), 3.0f, wave.getHeight());
        g.fillRoundedRectangle (x - 7.0f, wave.getCentreY() - 14.0f, 14.0f, 28.0f, 4.0f);
        g.setColour (juce::Colours::black);
        g.setFont (theme::heading (10.0f));
        g.drawText (x == xs ? "[" : "]",
                    juce::Rectangle<float> (x - 7.0f, wave.getCentreY() - 14.0f, 14.0f, 28.0f),
                    juce::Justification::centred);
    }

    // Fade grab triangles on the top edge, just inside each handle.
    auto fadeTri = [&g] (float x, bool leftEdge, bool active)
    {
        juce::Path tri;
        const float dir = leftEdge ? 1.0f : -1.0f;
        tri.addTriangle (x, 4.0f, x, 18.0f, x + dir * 14.0f, 4.0f);
        g.setColour (active ? theme::amberBright : theme::textDim);
        g.fillPath (tri, juce::AffineTransform::translation (0.0f, 0.0f));
    };
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.setOrigin (0, (int) waveRect().getY());
        fadeTri (xs + 4.0f, true, working.fadeIn > 0.001f);
        fadeTri (xe - 4.0f, false, working.fadeOut > 0.001f);
    }

    // Footer: region info + hint.
    const double lenSec = raw->buffer.getNumSamples() / raw->sourceSampleRate
                        * (working.end - working.start);
    g.setColour (theme::textDim);
    g.setFont (theme::label (11.0f));
    g.drawText ("kept: " + juce::String (lenSec, 2) + " s ("
                    + juce::String (juce::roundToInt ((working.end - working.start) * 100.0f))
                    + "%)      drag [ and ] to cut from both sides      "
                      "drag the small top triangles for a light fade      "
                      "REV / TRIM stay on the card",
                getLocalBounds().removeFromBottom (24).reduced (18, 0),
                juce::Justification::centredLeft);
}

void SampleEditPanel::resized()
{
    auto top = getLocalBounds().reduced (12).removeFromTop (26);
    closeButton.setBounds (top.removeFromRight (76));
    top.removeFromRight (6);
    resetButton.setBounds (top.removeFromRight (70));
}

void SampleEditPanel::mouseDown (const juce::MouseEvent& e)
{
    if (slot < 0)
        return;
    const auto wave = waveRect();
    const float xs = xForFrac (working.start);
    const float xe = xForFrac (working.end);
    dragging = Handle::none;
    if (! wave.expanded (10.0f).contains (e.position))
        return;

    // Fade triangles live on the top edge; edge handles win elsewhere.
    const bool nearTop = e.position.y < wave.getY() + 20.0f;
    if (nearTop && std::abs (e.position.x - (xs + 10.0f)) < 16.0f)
        dragging = Handle::fadeIn;
    else if (nearTop && std::abs (e.position.x - (xe - 10.0f)) < 16.0f)
        dragging = Handle::fadeOut;
    else if (std::abs (e.position.x - xs) < 12.0f)
        dragging = Handle::start;
    else if (std::abs (e.position.x - xe) < 12.0f)
        dragging = Handle::end;
    // Clicking inside the kept region grabs the nearer edge - fewer misses.
    else if (e.position.x > xs && e.position.x < xe)
        dragging = e.position.x - xs < xe - e.position.x ? Handle::start : Handle::end;
}

void SampleEditPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (slot < 0 || dragging == Handle::none)
        return;
    const float f = fracForX (e.position.x);
    const float span = juce::jmax (0.02f, working.end - working.start);
    switch (dragging)
    {
        case Handle::start:
            working.start = juce::jlimit (0.0f, working.end - 0.02f, f);
            break;
        case Handle::end:
            working.end = juce::jlimit (working.start + 0.02f, 1.0f, f);
            break;
        case Handle::fadeIn:
            working.fadeIn = juce::jlimit (0.0f, 0.5f, (f - working.start) / span);
            break;
        case Handle::fadeOut:
            working.fadeOut = juce::jlimit (0.0f, 0.5f, (working.end - f) / span);
            break;
        case Handle::none:
            break;
    }
    repaint();
}

void SampleEditPanel::mouseUp (const juce::MouseEvent&)
{
    if (slot >= 0 && dragging != Handle::none)
        apply();
    dragging = Handle::none;
}

} // namespace orcha
