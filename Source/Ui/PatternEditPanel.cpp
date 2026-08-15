#include "PatternEditPanel.h"

namespace orcha
{

PatternEditPanel::PatternEditPanel (OrchaAudioProcessor& p) : processor (p)
{
    resetButton.onClick = [this]
    {
        processor.resetOptionEdits (index);
        // The regenerated pattern arrives via the model callback; until then
        // keep showing the last edit.
    };
    addAndMakeVisible (resetButton);

    closeButton.setColour (juce::TextButton::buttonColourId, theme::amber);
    closeButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    // Baked-in polish per card, with an amount: reverb in red, delay in
    // blue. The render fires when the gesture ends, not on every pixel.
    auto initFxSlider = [this] (juce::Slider& s, juce::Colour colour)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (0.0, 1.0);
        s.setColour (juce::Slider::trackColourId, colour);
        s.setColour (juce::Slider::thumbColourId, colour.brighter (0.4f));
        s.setColour (juce::Slider::backgroundColourId, theme::panelLight);
        s.onDragEnd = [this]
        {
            if (index >= 0)
                processor.setOptionFx (index, (float) reverbSlider.getValue(),
                                       (float) delaySlider.getValue(),
                                       (float) pumpSlider.getValue());
        };
        addAndMakeVisible (s);
    };
    initFxSlider (reverbSlider, juce::Colour (0xffe85d4c));   // red
    initFxSlider (delaySlider, juce::Colour (0xff4da3ff));    // strong blue
    initFxSlider (pumpSlider, juce::Colour (0xffb07aff));     // purple

    // CLEAN: three strengths behind one small button - decoration strips
    // away, anchors and planned silence are untouchable, RESET undoes.
    cleanButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Light - ghosts and graces");
        m.addItem (2, "Medium - also quiet ornaments and soft rolls");
        m.addItem (3, "Hard - back to skeleton and strong motif");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (cleanButton),
            [this] (int choice)
            {
                if (choice > 0 && index >= 0)
                    processor.cleanOption (index, choice);
            });
    };
    addAndMakeVisible (cleanButton);

    // ENDING: where this loop is going. AUTO keeps the engine's choice.
    endingBox.addItem ("ENDING: AUTO", 1);
    endingBox.addItem ("ENDING: LOOP", 2);
    endingBox.addItem ("ENDING: DROP", 3);
    endingBox.addItem ("ENDING: BREAK", 4);
    endingBox.addItem ("ENDING: STOP", 5);
    endingBox.onChange = [this]
    {
        if (index >= 0 && endingBox.getSelectedId() > 0)
            processor.setOptionEnding (index, endingBox.getSelectedId() - 2);
    };
    addAndMakeVisible (endingBox);

    // B3: this card becomes a transition INTO the chosen card. Lands as an
    // edit, so RESET restores the original take.
    transitionBox.setTextWhenNothingSelected ("TRANSITION...");
    transitionBox.onChange = [this]
    {
        const int target = transitionBox.getSelectedId() - 1;
        if (index >= 0 && target >= 0 && target != index)
            processor.makeTransition (index, target);
        transitionBox.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (transitionBox);

    setVisible (false);
}

void PatternEditPanel::openFor (int optionIndex)
{
    if (optionIndex < 0 || optionIndex >= OrchaAudioProcessor::numOptions
        || ! processor.option (optionIndex).present)
        return;
    index = optionIndex;
    working = processor.option (optionIndex).pattern;
    reverbSlider.setValue (processor.option (optionIndex).fxReverb,
                           juce::dontSendNotification);
    delaySlider.setValue (processor.option (optionIndex).fxDelay,
                          juce::dontSendNotification);
    endingBox.setSelectedId (processor.option (optionIndex).endingOverride + 2,
                             juce::dontSendNotification);
    pumpSlider.setValue (processor.option (optionIndex).fxPump,
                         juce::dontSendNotification);
    transitionBox.clear (juce::dontSendNotification);
    for (int i = 0; i < OrchaAudioProcessor::numOptions; ++i)
        if (i != optionIndex && processor.option (i).present)
            transitionBox.addItem ("-> " + processor.option (i).pattern.name, i + 1);
    setVisible (true);
    toFront (false);
    repaint();
}

void PatternEditPanel::close()
{
    setVisible (false);
    index = -1;
}

void PatternEditPanel::refreshFromModel()
{
    if (index < 0 || ! isVisible())
        return;
    const auto& opt = processor.option (index);
    if (! opt.present)
    {
        close();
        return;
    }
    // Adopt the model's pattern unless the user is mid-gesture.
    if (pressed.lane < 0 && opt.ready)
    {
        working = opt.pattern;
        repaint();
    }
}

juce::Rectangle<float> PatternEditPanel::gridArea() const
{
    auto b = getLocalBounds().toFloat().reduced (14.0f);
    b.removeFromTop (40.0f);            // header
    b.removeFromLeft (96.0f);           // lane labels (sample names)
    b.removeFromBottom (22.0f);         // hint line
    return b;
}

PatternEditPanel::CellRef PatternEditPanel::cellAt (juce::Point<float> pos) const
{
    const auto grid = gridArea();
    if (! grid.contains (pos) || working.stepCount() <= 0)
        return {};
    CellRef c;
    c.step = juce::jlimit (0, working.stepCount() - 1,
        (int) ((pos.x - grid.getX()) / grid.getWidth() * (float) working.stepCount()));
    c.lane = juce::jlimit (0, 2, (int) ((pos.y - grid.getY()) / grid.getHeight() * 3.0f));
    return c;
}

Event* PatternEditPanel::eventInCell (Pattern& p, CellRef cell) const
{
    if (cell.lane < 0)
        return nullptr;
    for (auto& e : p.events)
        if (e.role == laneRole (cell.lane)
            && std::abs (e.pos - std::round (e.pos)) < 0.01
            && juce::roundToInt (e.pos) == cell.step)
            return &e;
    return nullptr;
}

void PatternEditPanel::applyWorking()
{
    if (index >= 0)
        processor.applyEditedPattern (index, working);
}

void PatternEditPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::background.withAlpha (0.98f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (theme::amber.withAlpha (0.7f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.5f);

    // Header.
    g.setColour (theme::text);
    g.setFont (theme::heading (16.0f));
    g.drawText ("EDIT  -  " + working.name
                    + (index >= 0 && processor.option (index).edited ? " *" : ""),
                18, 12, 300, 22, juce::Justification::centredLeft);

    // FX captions, matching their slider colours.
    g.setFont (theme::heading (10.0f));
    g.setColour (juce::Colour (0xffe85d4c));
    g.drawText ("REVERB", reverbSlider.getX() - 2, reverbSlider.getY() - 11,
                reverbSlider.getWidth(), 12, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff4da3ff));
    g.drawText ("DELAY", delaySlider.getX() - 2, delaySlider.getY() - 11,
                delaySlider.getWidth(), 12, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffb07aff));
    g.drawText ("PUMP", pumpSlider.getX() - 2, pumpSlider.getY() - 11,
                pumpSlider.getWidth(), 12, juce::Justification::centredLeft);

    const auto grid = gridArea();
    const int steps = juce::jmax (1, working.stepCount());
    const float cellW = grid.getWidth() / (float) steps;
    const float laneH = grid.getHeight() / 3.0f;

    // Each lane is labeled with the sample that actually plays it, not the
    // abstract role - the user thinks in their samples.
    for (int lane = 0; lane < 3; ++lane)
    {
        const float y = grid.getY() + lane * laneH;
        const auto labelArea = juce::Rectangle<float> (grid.getX() - 96.0f, y,
                                                       88.0f, laneH);
        const int slot = processor.slotForRole (laneRole (lane));
        const auto sample = slot >= 0 ? processor.getSample (slot) : nullptr;

        g.setColour (theme::text);
        g.setFont (theme::heading (11.0f));
        g.drawText (sample != nullptr ? "SAMPLE " + juce::String (slot + 1)
                                      : juce::String (roleName (laneRole (lane))),
                    labelArea.withHeight (laneH * 0.5f).withTrimmedTop (laneH * 0.5f - 22.0f),
                    juce::Justification::bottomLeft);
        if (sample != nullptr)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::label (9.5f));
            g.drawText (sample->file.getFileNameWithoutExtension(),
                        labelArea.withTrimmedTop (laneH * 0.5f).withHeight (14.0f),
                        juce::Justification::topLeft);
        }
        g.setColour (theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) y, grid.getX(), grid.getRight());
    }

    // Step columns: beats brighter, bar lines brighter still.
    for (int s = 0; s <= steps; ++s)
    {
        const float x = grid.getX() + s * cellW;
        const bool barLine = s % 16 == 0;
        const bool beat = s % 4 == 0;
        g.setColour (theme::outline.withAlpha (barLine ? 0.9f : beat ? 0.55f : 0.22f));
        g.drawVerticalLine ((int) x, grid.getY(), grid.getBottom());
    }
    g.setColour (theme::outline.withAlpha (0.5f));
    g.drawHorizontalLine ((int) grid.getBottom(), grid.getX(), grid.getRight());

    // Events.
    for (const auto& e : working.events)
    {
        const int lane = e.role == Role::HIGH ? 0 : e.role == Role::MID ? 1
                       : e.role == Role::LOW ? 2 : -1;
        if (lane < 0)
            continue;
        const float laneY = grid.getY() + lane * laneH;
        const bool onGrid = std::abs (e.pos - std::round (e.pos)) < 0.01;
        const auto colour = lane == 0 ? theme::turquoise
                          : lane == 1 ? theme::amber
                                      : juce::Colour (0xffe85d4c);
        if (onGrid)
        {
            const float h = juce::jmax (4.0f, (laneH - 10.0f) * e.velocity);
            juce::Rectangle<float> bar (grid.getX() + (float) e.pos * cellW + 2.0f,
                                        laneY + laneH - 5.0f - h,
                                        cellW - 4.0f, h);
            g.setColour (colour.withAlpha (0.45f + 0.5f * e.velocity));
            g.fillRoundedRectangle (bar, 2.0f);
        }
        else
        {
            // Fractional events (rolls, graces): thin ticks, not editable.
            g.setColour (colour.withAlpha (0.5f));
            g.fillRect (grid.getX() + (float) e.pos * cellW, laneY + laneH * 0.35f,
                        2.0f, laneH * 0.6f - 5.0f);
        }
    }

    g.setColour (theme::textDim);
    g.setFont (theme::label (11.0f));
    g.drawText ("click: add / remove      drag up-down on a hit: velocity      "
                "thin ticks are rolls (regenerate to change them)",
                getLocalBounds().removeFromBottom (24).reduced (14, 0),
                juce::Justification::centredLeft);
}

void PatternEditPanel::resized()
{
    auto top = getLocalBounds().reduced (12).removeFromTop (26);
    closeButton.setBounds (top.removeFromRight (76));
    top.removeFromRight (6);
    resetButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (16);
    delaySlider.setBounds (top.removeFromRight (110));
    top.removeFromRight (44);   // room for the DELAY caption (painted)
    reverbSlider.setBounds (top.removeFromRight (110));
    top.removeFromRight (10);
    pumpSlider.setBounds (top.removeFromRight (80));
    top.removeFromRight (36);   // PUMP caption (painted)
    endingBox.setBounds (top.removeFromRight (122));
    top.removeFromRight (6);
    cleanButton.setBounds (top.removeFromRight (58));
    top.removeFromRight (6);
    transitionBox.setBounds (top.removeFromRight (118));
}

void PatternEditPanel::mouseDown (const juce::MouseEvent& e)
{
    pressed = cellAt (e.position);
    draggedVelocity = false;
    pendingAdd = pressed.lane >= 0 && eventInCell (working, pressed) == nullptr;
}

void PatternEditPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed.lane < 0 || pendingAdd || e.getDistanceFromDragStart() < 3)
        return;
    if (auto* ev = eventInCell (working, pressed))
    {
        // Velocity from the vertical position inside the lane.
        const auto grid = gridArea();
        const float laneH = grid.getHeight() / 3.0f;
        const float laneTop = grid.getY() + pressed.lane * laneH;
        ev->velocity = juce::jlimit (0.05f, 1.0f,
            1.0f - (e.position.y - laneTop - 5.0f) / (laneH - 10.0f));
        draggedVelocity = true;
        repaint();
    }
}

void PatternEditPanel::mouseUp (const juce::MouseEvent&)
{
    if (pressed.lane < 0)
        return;

    if (draggedVelocity)
    {
        applyWorking();                       // velocity gesture ends: render
    }
    else if (pendingAdd)
    {
        Event ev;
        ev.pos = pressed.step;
        ev.role = laneRole (pressed.lane);
        ev.velocity = 0.7f;
        if (ev.role == Role::HIGH)
            ev.gateSteps = 0.75;
        working.events.push_back (ev);
        std::sort (working.events.begin(), working.events.end(), eventBefore);
        applyWorking();
    }
    else if (auto* ev = eventInCell (working, pressed))
    {
        const auto pos = ev->pos;
        const auto role = ev->role;
        working.events.erase (std::remove_if (working.events.begin(), working.events.end(),
            [pos, role] (const Event& x)
            { return x.role == role && std::abs (x.pos - pos) < 0.01; }),
            working.events.end());
        applyWorking();
    }
    else
    {
        // F2: a click that lands on (or near) a thin off-grid tick deletes
        // it - graces and roll hits are now editable-away, one by one.
        const Role lane = laneRole (pressed.lane);
        double bestDist = 0.6;
        int bestIdx = -1;
        for (int k = 0; k < (int) working.events.size(); ++k)
        {
            const auto& e = working.events[(size_t) k];
            if (e.role != lane
                || std::abs (e.pos - std::round (e.pos)) < 0.01)
                continue;
            const double d = std::abs (e.pos - ((double) pressed.step + 0.5));
            if (d < bestDist)
            {
                bestDist = d;
                bestIdx = k;
            }
        }
        if (bestIdx >= 0)
        {
            working.events.erase (working.events.begin() + bestIdx);
            applyWorking();
        }
    }

    pressed = {};
    pendingAdd = false;
    draggedVelocity = false;
    repaint();
}

} // namespace orcha
