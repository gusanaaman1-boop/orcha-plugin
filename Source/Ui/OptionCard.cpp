#include "OptionCard.h"

namespace orcha
{

OptionCard::OptionCard (int indexIn) : index (indexIn) {}

void OptionCard::update (PreviewPlayer::Loop::Ptr loopIn, const juce::String& nameIn,
                         bool presentIn, bool readyIn, bool favoriteIn, bool playingIn)
{
    // The editor pushes updates on a timer; only actual changes repaint.
    if (loop == loopIn && name == nameIn && present == presentIn
        && ready == readyIn && favorite == favoriteIn && playing == playingIn)
        return;
    if (loop != loopIn)
        waveImage = juce::Image();   // audio changed: re-render the cache
    loop = std::move (loopIn);
    name = nameIn;
    present = presentIn;
    ready = readyIn;
    favorite = favoriteIn;
    playing = playingIn;
    repaint();
}

juce::Rectangle<float> OptionCard::playArea() const
{
    return { 10.0f, (float) getHeight() * 0.5f - 26.0f, 34.0f, 34.0f };
}

juce::Rectangle<float> OptionCard::heartArea() const
{
    return { (float) getWidth() - 30.0f, 8.0f, 22.0f, 20.0f };
}

juce::Rectangle<float> OptionCard::regenArea() const
{
    return { (float) getWidth() - 30.0f, 32.0f, 22.0f, 20.0f };
}

juce::Rectangle<float> OptionCard::dragArea() const
{
    return { (float) getWidth() - 64.0f, (float) getHeight() - 26.0f, 56.0f, 20.0f };
}

juce::Rectangle<float> OptionCard::waveBounds() const
{
    const auto pa = playArea();
    return { pa.getRight() + 8.0f, 12.0f,
             (float) getWidth() - pa.getRight() - 46.0f,
             (float) getHeight() - 46.0f };
}

void OptionCard::setPlayhead (float fraction)
{
    if (! playing || std::abs (fraction - playhead) < 0.002f)
        return;
    playhead = fraction;
    repaint (waveBounds().expanded (3.0f).toNearestInt());
}

juce::Rectangle<float> OptionCard::midiArea() const
{
    return { (float) getWidth() - 104.0f, (float) getHeight() - 26.0f, 36.0f, 20.0f };
}

juce::Rectangle<float> OptionCard::editArea() const
{
    return { (float) getWidth() - 144.0f, (float) getHeight() - 26.0f, 36.0f, 20.0f };
}

void OptionCard::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    // Neon means ONE light source in a dark room. Twelve glowing frames at
    // once read as strain (tried, rejected). Idle cards stay quiet - a thin
    // outline, faintly tinted; only the playing card lights up, and a
    // favorite gets a calm amber edge.
    if (playing)
        theme::neonRect (g, bounds.reduced (1.5f), 7.0f,
                         theme::waveColour (index), 1.0f);
    else
    {
        g.setColour (favorite ? theme::amber.withAlpha (0.7f)
                              : theme::waveColour (index).withAlpha (0.22f));
        g.drawRoundedRectangle (bounds, 8.0f, favorite ? 1.3f : 1.0f);
    }

    if (! present)
    {
        g.setColour (theme::textDim.withAlpha (0.6f));
        g.setFont (theme::label (12.0f));
        g.drawText ("-", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Play / stop.
    auto pa = playArea();
    g.setColour (playing ? theme::waveColour (index).withAlpha (0.22f)
                         : theme::panelLight);
    g.fillEllipse (pa);
    if (playing)
    {
        g.setColour (theme::waveColour (index).withAlpha (0.6f));
        g.drawEllipse (pa.reduced (0.5f), 1.0f);
    }
    g.setColour (ready ? theme::text : theme::textDim);
    if (! ready)
    {
        g.setFont (theme::label (11.0f));
        g.drawText ("...", pa, juce::Justification::centred);
    }
    else if (playing)
        g.fillRect (pa.reduced (11.0f));
    else
    {
        juce::Path tri;
        const auto c = pa.getCentre();
        tri.addTriangle (c.x - 5.0f, c.y - 7.0f, c.x - 5.0f, c.y + 7.0f, c.x + 8.0f, c.y);
        g.fillPath (tri);
    }

    // Waveform, cached as an image so the playhead can move at frame rate.
    const auto wave = waveBounds();
    if (ready && loop != nullptr)
    {
        const int w = juce::jmax (1, (int) wave.getWidth());
        const int h = juce::jmax (1, (int) wave.getHeight());
        if (! waveImage.isValid() || waveImage.getWidth() != w * 2)
        {
            waveImage = juce::Image (juce::Image::ARGB, w * 2, h * 2, true);
            juce::Graphics ig (waveImage);
            theme::paintWaveform (ig, { 0.0f, 0.0f, (float) w * 2, (float) h * 2 },
                                  loop->buffer,
                                  theme::waveColour (index).withAlpha (0.95f));
        }
        g.drawImage (waveImage, wave);
        if (playing)
        {
            const float px = wave.getX() + wave.getWidth()
                                 * juce::jlimit (0.0f, 1.0f, playhead);
            // Neon playhead: bloom, then the bright core line.
            g.setColour (theme::amberBright.withAlpha (0.18f));
            g.fillRect (px - 4.0f, wave.getY(), 8.0f, wave.getHeight());
            g.setColour (theme::amberBright.withAlpha (0.45f));
            g.fillRect (px - 2.0f, wave.getY(), 4.0f, wave.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.fillRect (px - 0.75f, wave.getY(), 1.5f, wave.getHeight());
        }
    }
    else
    {
        g.setColour (theme::textDim);
        g.setFont (theme::label (11.0f));
        g.drawText ("rendering...", wave, juce::Justification::centred);
    }

    // Name.
    g.setColour (theme::text);
    g.setFont (theme::heading (13.0f));
    g.drawText (name, juce::Rectangle<float> (12.0f, (float) getHeight() - 26.0f,
                                              juce::jmax (50.0f, (float) getWidth() - 160.0f),
                                              18.0f),
                juce::Justification::centredLeft);

    // Favorite heart.
    {
        auto ha = heartArea().reduced (4.0f);
        juce::Path heart;
        const float w = ha.getWidth(), h = ha.getHeight();
        heart.startNewSubPath (ha.getX() + w * 0.5f, ha.getBottom());
        heart.cubicTo (ha.getX() - w * 0.25f, ha.getY() + h * 0.3f,
                       ha.getX() + w * 0.2f, ha.getY() - h * 0.3f,
                       ha.getX() + w * 0.5f, ha.getY() + h * 0.25f);
        heart.cubicTo (ha.getX() + w * 0.8f, ha.getY() - h * 0.3f,
                       ha.getRight() + w * 0.25f, ha.getY() + h * 0.3f,
                       ha.getX() + w * 0.5f, ha.getBottom());
        heart.closeSubPath();
        if (favorite)
        {
            theme::neonPath (g, heart, theme::amber, 0.6f);
            g.setColour (theme::amberBright);
            g.fillPath (heart);
        }
        else
        {
            g.setColour (theme::textDim);
            g.strokePath (heart, juce::PathStrokeType (1.4f));
        }
    }

    // Regenerate-this-option: circular arrow.
    {
        auto ra = regenArea().reduced (5.0f);
        const auto c = ra.getCentre();
        const float radius = juce::jmin (ra.getWidth(), ra.getHeight()) * 0.5f;
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.6f, 5.2f, true);
        g.setColour (theme::textDim);
        g.strokePath (arc, juce::PathStrokeType (1.6f));
        juce::Path head;
        head.addTriangle (0.0f, -4.0f, 5.0f, 0.0f, 0.0f, 4.0f);
        g.fillPath (head, juce::AffineTransform::rotation (0.6f)
                        .translated (c.x + radius * std::sin (0.6f),
                                     c.y - radius * std::cos (0.6f)));
    }

    // EDIT chip: opens the per-option step editor.
    {
        auto ea = editArea();
        g.setColour (theme::panelLight.withAlpha (0.8f));
        g.fillRoundedRectangle (ea, 4.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (ea, 4.0f, 1.0f);
        g.setColour (ready ? theme::text : theme::textDim);
        g.setFont (theme::heading (10.0f));
        g.drawText ("EDIT", ea, juce::Justification::centred);
    }

    // MIDI drag handle: same gesture, lands as editable notes instead of audio.
    {
        auto ma = midiArea();
        g.setColour (theme::panelLight.withAlpha (0.8f));
        g.fillRoundedRectangle (ma, 4.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (ma, 4.0f, 1.0f);
        g.setColour (ready ? theme::turquoise.withAlpha (0.9f) : theme::textDim);
        g.setFont (theme::heading (10.0f));
        g.drawText ("MIDI", ma, juce::Justification::centred);
    }

    // Drag handle.
    {
        auto da = dragArea();
        g.setColour (dragging ? theme::amber.withAlpha (0.25f) : theme::panelLight);
        g.fillRoundedRectangle (da, 4.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (da, 4.0f, 1.0f);
        g.setColour (ready ? theme::text : theme::textDim);
        g.setFont (theme::heading (11.0f));
        g.drawText ("DRAG", da, juce::Justification::centred);
        // Grip dots.
        g.setColour (theme::textDim);
        for (int row = 0; row < 3; ++row)
        {
            g.fillEllipse (da.getX() + 6.0f, da.getY() + 5.0f + row * 5.0f, 2.0f, 2.0f);
            g.fillEllipse (da.getRight() - 8.0f, da.getY() + 5.0f + row * 5.0f, 2.0f, 2.0f);
        }
    }
}

void OptionCard::mouseUp (const juce::MouseEvent& e)
{
    if (! present || dragging)
    {
        dragging = false;
        return;
    }
    const auto pos = e.position;
    if (playArea().contains (pos) && ready)          { if (onPlay) onPlay(); }
    else if (heartArea().expanded (3.0f).contains (pos)) { if (onFavorite) onFavorite(); }
    else if (regenArea().expanded (3.0f).contains (pos)) { if (onRegenerate) onRegenerate(); }
    else if (editArea().contains (pos) && ready)     { if (onEdit) onEdit(); }
}

void OptionCard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! ready || getDragFile == nullptr
        || e.getDistanceFromDragStart() < 8)
        return;
    // Buttons stay clickable: drags only start from the handles or waveform,
    // not from the play/favorite/regenerate hotspots.
    if (playArea().contains (e.mouseDownPosition)
        || heartArea().contains (e.mouseDownPosition)
        || regenArea().contains (e.mouseDownPosition)
        || editArea().contains (e.mouseDownPosition))
        return;

    // A drag that begins on the MIDI chip carries the .mid; anywhere else
    // carries the rendered WAV.
    const bool wantMidi = midiArea().contains (e.mouseDownPosition)
                          && getMidiDragFile != nullptr;
    const auto file = wantMidi ? getMidiDragFile() : getDragFile();
    if (! file.existsAsFile())
        return;

    dragging = true;
    repaint();
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        container->performExternalDragDropOfFiles ({ file.getFullPathName() },
                                                   /*canMoveFiles*/ false, this,
                                                   [this] (auto&&...)
                                                   {
                                                       dragging = false;
                                                       repaint();
                                                   });
}

void OptionCard::mouseMove (const juce::MouseEvent& e)
{
    const bool overHandle = ready && dragArea().contains (e.position);
    setMouseCursor (overHandle ? juce::MouseCursor::DraggingHandCursor
                               : juce::MouseCursor::NormalCursor);
}

} // namespace orcha
