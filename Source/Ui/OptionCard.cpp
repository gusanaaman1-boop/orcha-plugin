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
    return { (float) getWidth() - 76.0f, (float) getHeight() - 26.0f, 68.0f, 20.0f };
}

void OptionCard::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (playing ? theme::amber : theme::outline);
    g.drawRoundedRectangle (bounds, 8.0f, playing ? 1.5f : 1.0f);

    if (! present)
    {
        g.setColour (theme::textDim.withAlpha (0.6f));
        g.setFont (theme::label (12.0f));
        g.drawText ("-", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Play / stop.
    auto pa = playArea();
    g.setColour (theme::panelLight);
    g.fillEllipse (pa);
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

    // Waveform.
    auto wave = juce::Rectangle<float> (pa.getRight() + 8.0f, 12.0f,
                                        (float) getWidth() - pa.getRight() - 46.0f,
                                        (float) getHeight() - 46.0f);
    if (ready && loop != nullptr)
        theme::paintWaveform (g, wave, loop->buffer,
                              theme::waveColour (index).withAlpha (0.95f));
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
                                              120.0f, 18.0f),
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
            g.setColour (theme::amber);
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
}

void OptionCard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! ready || getDragFile == nullptr
        || e.getDistanceFromDragStart() < 8)
        return;
    // Buttons stay clickable: drags only start from the handle or waveform,
    // not from the play/favorite/regenerate hotspots.
    if (playArea().contains (e.mouseDownPosition)
        || heartArea().contains (e.mouseDownPosition)
        || regenArea().contains (e.mouseDownPosition))
        return;

    const auto file = getDragFile();
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
