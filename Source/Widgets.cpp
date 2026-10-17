#include "Widgets.h"

//==============================================================================
ParamControl::ParamControl (EditorContext& c, Module& m, ModParam& p)
    : module (m), param (p), ctx (c)
{
    setRepaintsOnMouseActivity (true);
}

ParamControl::~ParamControl()
{
    if (dragging) param.endGesture();
}

juce::String ParamControl::getDisplayText() const
{
    return param.textForReal (dragging ? param.getBaseReal() : param.getEffReal());
}

void ParamControl::mouseDown (const juce::MouseEvent& e)
{
    if (ctx.isLearning())
    {
        ctx.learnClick (module, param);
        return;
    }

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader (module.getName() + " - " + param.name);
        m.addItem (1, "Type a value...");
        m.addItem (2, "Reset to default");
        juce::Component::SafePointer<ParamControl> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->startTextEdit();
            if (r == 2)
            {
                safe->param.beginGesture();
                safe->param.setBaseNorm (safe->param.range.convertTo0to1 (safe->param.defaultValue));
                safe->param.endGesture();
                safe->repaint();
            }
        });
        return;
    }

    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        param.beginGesture();
        param.setBaseNorm (param.range.convertTo0to1 (param.defaultValue));
        param.endGesture();
        repaint();
        return;
    }

    startNorm = param.getBaseNorm();
    dragging = true;
    param.beginGesture();
}

void ParamControl::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float k = e.mods.isShiftDown() ? sensitivity * 0.15f : sensitivity;
    param.setBaseNorm (startNorm - (float) e.getDistanceFromDragStartY() * k);
    repaint();
}

void ParamControl::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    param.endGesture();
    repaint();
}

void ParamControl::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (ctx.isLearning() || e.mods.isPopupMenu()) return;
    startTextEdit();
}

void ParamControl::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (ctx.isLearning()) return;
    const float k = e.mods.isShiftDown() ? 0.002f : 0.02f;
    param.beginGesture();
    param.setBaseNorm (param.getBaseNorm() + (w.deltaY != 0.0f ? w.deltaY : -w.deltaX) * k * 5.0f);
    param.endGesture();
    repaint();
}

void ParamControl::startTextEdit()
{
    textEditor = std::make_unique<juce::TextEditor>();
    auto& te = *textEditor;
    addAndMakeVisible (te);
    te.setBounds (getEditorBounds());
    te.setFont (Theme::mono (12.0f));
    te.setJustification (juce::Justification::centred);
    te.setIndents (2, 1);
    te.setText (param.textForReal (param.getBaseReal()), false);
    te.selectAll();
    te.grabKeyboardFocus();

    juce::Component::SafePointer<ParamControl> safe (this);
    auto close = [safe] (bool apply)
    {
        juce::MessageManager::callAsync ([safe, apply]
        {
            if (safe == nullptr || safe->textEditor == nullptr) return;
            if (apply)
            {
                safe->param.beginGesture();
                safe->param.setFromText (safe->textEditor->getText());
                safe->param.endGesture();
            }
            safe->textEditor.reset();
            safe->repaint();
        });
    };
    te.onReturnKey = [close] { close (true); };
    te.onEscapeKey = [close] { close (false); };
    te.onFocusLost = [close] { close (true); };
}

void ParamControl::paintOverChildren (juce::Graphics& g)
{
    if (ctx.isLearning() && ctx.canBeLearnTarget (module, param))
    {
        const float a = 0.35f + 0.65f * ctx.getFlash();
        g.setColour (Theme::yellow.withAlpha (a));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.6f);
        if (isMouseOver())
        {
            g.setColour (Theme::yellow.withAlpha (0.15f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        }
    }
}

//==============================================================================
ValueBox::ValueBox (EditorContext& c, Module& m, ModParam& p, Style s, juce::String l)
    : ParamControl (c, m, p), style (s), label (l.isEmpty() ? p.name : l)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip (p.name + " - drag, scroll, or double-click to type");
}

juce::Rectangle<int> ValueBox::getEditorBounds()
{
    return style == Style::Boxed ? getLocalBounds() : getLocalBounds().removeFromBottom (getHeight() / 2 + 2);
}

void ValueBox::paint (juce::Graphics& g)
{
    const bool modulated = std::abs (param.effNorm.load() - param.getBaseNorm()) > 1.0e-4f;
    const auto valueColour = modulated ? Theme::yellow : Theme::text;
    auto r = getLocalBounds().toFloat();

    if (style == Style::Boxed)
    {
        g.setColour (isMouseOver() ? Theme::valueBox.brighter (0.1f) : Theme::valueBox);
        g.fillRoundedRectangle (r.reduced (0.5f), 4.0f);
        g.setColour (valueColour);
        g.setFont (Theme::mono (juce::jmin (14.0f, r.getHeight() * 0.62f)));
        g.drawFittedText (getDisplayText(), getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.7f);
    }
    else
    {
        auto top = r.removeFromTop (r.getHeight() * 0.5f);
        g.setColour (Theme::value);
        g.setFont (Theme::mono (12.0f));
        g.drawFittedText (label, top.toNearestInt(), juce::Justification::centredBottom, 1, 0.6f);
        g.setColour (valueColour);
        g.setFont (Theme::mono (12.0f));
        g.drawFittedText (getDisplayText(), r.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
    }
}

//==============================================================================
ModKnob::ModKnob (EditorContext& c, Module& m, ModParam& p, bool bip, juce::Colour col)
    : ParamControl (c, m, p), bipolar (bip), arcColour (col)
{
    sensitivity = 0.004f;
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void ModKnob::paint (juce::Graphics& g)
{
    const float size = (float) juce::jmin (getWidth(), getHeight()) - 4.0f;
    const auto c = getLocalBounds().toFloat().getCentre();
    const float R = size * 0.5f;
    const float a0 = -2.356f, a1 = 2.356f;
    auto angleFor = [&] (float norm) { return a0 + norm * (a1 - a0); };

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, R - 2.0f, R - 2.0f, 0.0f, a0, a1, true);
    g.setColour (juce::Colour (0xff2c2c33));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const float base = param.getBaseNorm(), effv = param.effNorm.load();
    const float from = bipolar ? 0.0f : a0;
    const float to = angleFor (effv);
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, R - 2.0f, R - 2.0f, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    g.setColour (arcColour);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // body
    const float br = R * 0.72f;
    juce::ColourGradient grad (juce::Colour (0xff3a3d45), c.x - br * 0.5f, c.y - br * 0.6f,
                               juce::Colour (0xff15161a), c.x + br * 0.4f, c.y + br, true);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (juce::Colour (0xff0b0b0e));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.5f);

    // pointer (the user's setting)
    const float pa = angleFor (base);
    juce::Path ptr;
    ptr.addRoundedRectangle (-2.0f, -br + 3.0f, 4.0f, br * 0.36f, 1.5f);
    g.setColour (Theme::macro);
    g.fillPath (ptr, juce::AffineTransform::rotation (pa).translated (c.x, c.y));

    // modulated position
    if (std::abs (effv - base) > 1.0e-4f)
    {
        const float ea = angleFor (effv);
        const float dx = std::sin (ea) * (R - 2.0f), dy = -std::cos (ea) * (R - 2.0f);
        g.setColour (Theme::orange);
        g.fillEllipse (c.x + dx - 3.0f, c.y + dy - 3.0f, 6.0f, 6.0f);
    }
}

//==============================================================================
DepthDial::DepthDial (EditorContext& c, int uid, bool in)
    : assignmentUid (uid), incoming (in), ctx (c)
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

    if (auto* a = get())
    {
        if (a->target == Assignment::Target::HostParam) label = "P";
        else if (a->dst != nullptr) label = a->dst->getAbbrev();
    }
}

Assignment* DepthDial::get() const
{
    return ctx.getProcessor().engine.findAssignment (assignmentUid);
}

void DepthDial::paint (juce::Graphics& g)
{
    auto* a = get();
    if (a == nullptr) return;

    const auto col = incoming ? Theme::orange : Theme::yellow;
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const auto c = r.getCentre();
    const float R = r.getWidth() * 0.5f;
    const float depth = a->depth.load();

    g.setColour (juce::Colour (0xff1c1912));
    g.fillEllipse (r);

    if (std::abs (depth) > 0.001f)
    {
        juce::Path wedge;
        wedge.addPieSegment (r.reduced (incoming ? 1.0f : 2.0f), 0.0f, depth * juce::MathConstants<float>::twoPi * 0.999f, 0.0f);
        g.setColour (col.withAlpha (0.45f));
        g.fillPath (wedge);
    }

    g.setColour (col.withAlpha (isMouseOver (true) ? 1.0f : 0.85f));
    g.drawEllipse (r, incoming ? 1.4f : 1.8f);

    if (! incoming)
    {
        g.setColour (col);
        g.setFont (Theme::mono (label.length() > 2 ? 9.0f : 11.0f));
        g.drawText (label, getLocalBounds(), juce::Justification::centred);
    }
    juce::ignoreUnused (c, R);
}

void DepthDial::updateBubble()
{
    auto* a = get();
    if (a == nullptr) return;
    auto& proc = ctx.getProcessor();
    juce::String title, sub;

    if (incoming)
    {
        title = "RECEIVE";
        if (a->src != nullptr)
            sub = a->src->getName() + (a->src->getNumOutputs() > 1 ? " " + a->src->getOutputName (a->sourceOutput) : juce::String());
    }
    else if (a->target == Assignment::Target::HostParam)
    {
        title = proc.getHostedName();
        sub = proc.getHostedParamName (a->hostIndex);
    }
    else if (a->dst != nullptr)
    {
        title = a->dst->getName();
        sub = a->dst->params[(size_t) a->dstParamIndex]->name;
    }

    ctx.showBubble (*this, title, sub, Theme::signedValue (a->depth.load() * 100.0f));
}

void DepthDial::setDepth (float d)
{
    if (auto* a = get())
    {
        a->depth.store (juce::jlimit (-1.0f, 1.0f, d));
        repaint();
        updateBubble();
        // the twin dial (incoming/outgoing) lives in another panel
        if (auto* top = getTopLevelComponent()) top->repaint();
    }
}

void DepthDial::mouseEnter (const juce::MouseEvent&) { updateBubble(); }
void DepthDial::mouseExit (const juce::MouseEvent&)  { ctx.hideBubble (*this); }

void DepthDial::mouseDown (const juce::MouseEvent& e)
{
    auto* a = get();
    if (a == nullptr) return;
    startDepth = a->depth.load();

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Invert");
        m.addItem (2, "Reset to 0");
        m.addItem (3, "Set amount...");
        m.addSeparator();
        m.addItem (4, "Remove modulation");
        juce::Component::SafePointer<DepthDial> safe (this);
        auto& c = ctx;
        const int uid = assignmentUid;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, &c, uid] (int r)
        {
            if (r == 4) { if (safe != nullptr) c.hideBubble (*safe.getComponent()); c.getProcessor().engine.removeAssignment (uid); c.structureChanged(); return; }
            if (safe == nullptr) return;
            auto* asg = safe->get();
            if (asg == nullptr) return;
            if (r == 1) safe->setDepth (-asg->depth.load());
            if (r == 2) safe->setDepth (0.0f);
            if (r == 3)
            {
                auto* aw = new juce::AlertWindow ("Modulation amount", "Enter an amount from -100 to +100", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor ("v", juce::String (asg->depth.load() * 100.0f, 2));
                aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int res)
                {
                    if (res == 1 && safe != nullptr)
                        safe->setDepth (aw->getTextEditorContents ("v").getFloatValue() / 100.0f);
                }), true);
            }
        });
    }
}

void DepthDial::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const float k = e.mods.isShiftDown() ? 0.0008f : 0.005f;
    setDepth (startDepth - (float) e.getDistanceFromDragStartY() * k);
}

void DepthDial::mouseUp (const juce::MouseEvent&) {}

void DepthDial::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) setDepth (0.0f);
}

void DepthDial::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (auto* a = get())
        setDepth (a->depth.load() + w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.1f));
}

//==============================================================================
void HoverBubble::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xf0141418));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colour (0xff45454f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    auto area = getLocalBounds().reduced (8, 5);
    g.setColour (Theme::text);
    g.setFont (Theme::mono (11.0f));
    g.drawFittedText (title, area.removeFromTop (14), juce::Justification::centredLeft, 1, 0.6f);
    g.setColour (Theme::dim);
    g.setFont (Theme::mono (10.0f, false));
    g.drawFittedText (sub, area.removeFromTop (14), juce::Justification::centredLeft, 1, 0.6f);
    g.setColour (Theme::green);
    g.setFont (Theme::mono (12.0f));
    g.drawText (value, area, juce::Justification::centred);
}

//==============================================================================
void PathButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (drawBackground || down)
    {
        g.setColour (down ? Theme::border : over ? Theme::panelTop : Theme::panel);
        g.fillRoundedRectangle (r.reduced (0.5f), 4.0f);
    }
    if (painter)
        painter (g, r, (over || getToggleState()) && isEnabled() ? overColour : colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
}

void PowerLed::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (13.0f, 13.0f);
    if (getToggleState())
    {
        g.setColour (Theme::green.withAlpha (0.25f));
        g.fillEllipse (r.expanded (2.0f));
        g.setColour (Theme::green.darker (0.6f));
        g.drawEllipse (r, 2.0f);
        g.setColour (Theme::green);
        g.fillEllipse (r.reduced (2.5f));
    }
    else
    {
        g.setColour (juce::Colour (0xff2a2a30));
        g.fillEllipse (r);
        g.setColour (over ? Theme::dim : Theme::faint);
        g.drawEllipse (r, 1.5f);
    }
}

void AssignButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float flash = learning ? ctx.getFlash() : 0.0f;

    g.setColour (learning ? colour.withAlpha (0.35f + 0.6f * flash) : (over ? colour.withAlpha (0.25f) : juce::Colour (0xff3a3016)));
    g.fillRoundedRectangle (r, 3.5f);
    g.setColour (colour.withAlpha (down ? 1.0f : 0.75f));
    g.drawRoundedRectangle (r, 3.5f, 1.0f);
    g.setColour (learning && flash > 0.5f ? juce::Colour (0xff1a1508) : colour);
    g.setFont (Theme::mono (11.5f));
    g.drawText (getName(), getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
namespace Icons
{
    void gear (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        auto b = r.withSizeKeepingCentre (18.0f, 18.0f);
        const auto ctr = b.getCentre();
        juce::Path p;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (teeth * 2);
            const float rad = (i % 2 == 0) ? 8.5f : 6.5f;
            const float a2 = a + juce::MathConstants<float>::twoPi / (teeth * 2);
            if (i == 0) p.startNewSubPath (ctr.x + rad * std::sin (a), ctr.y - rad * std::cos (a));
            else p.lineTo (ctr.x + rad * std::sin (a), ctr.y - rad * std::cos (a));
            p.lineTo (ctr.x + rad * std::sin (a2), ctr.y - rad * std::cos (a2));
        }
        p.closeSubPath();
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.5f));
        g.drawEllipse (ctr.x - 3.0f, ctr.y - 3.0f, 6.0f, 6.0f, 1.5f);
    }

    void plus (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        auto b = r.withSizeKeepingCentre (10.0f, 10.0f);
        g.setColour (c);
        g.drawLine (b.getX(), b.getCentreY(), b.getRight(), b.getCentreY(), 2.0f);
        g.drawLine (b.getCentreX(), b.getY(), b.getCentreX(), b.getBottom(), 2.0f);
    }

    void list (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        auto b = r.withSizeKeepingCentre (14.0f, 11.0f);
        g.setColour (c);
        for (int i = 0; i < 3; ++i)
        {
            const float y = b.getY() + (float) i * 5.0f + 0.5f;
            g.fillEllipse (b.getX(), y - 1.2f, 2.4f, 2.4f);
            g.fillRect (b.getX() + 5.0f, y - 1.0f, 9.0f, 2.0f);
        }
    }

    void cross (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        auto b = r.withSizeKeepingCentre (9.0f, 9.0f);
        g.setColour (c);
        g.drawLine (b.getX(), b.getY(), b.getRight(), b.getBottom(), 2.0f);
        g.drawLine (b.getRight(), b.getY(), b.getX(), b.getBottom(), 2.0f);
    }

    void collapse (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        auto b = r.withSizeKeepingCentre (14.0f, 14.0f);
        g.setColour (c);
        g.drawRoundedRectangle (b, 2.0f, 1.4f);
        g.drawLine (b.getX() + 3.5f, b.getBottom() - 3.5f, b.getRight() - 3.5f, b.getY() + 3.5f, 1.4f);
        g.drawLine (b.getRight() - 3.5f, b.getY() + 3.5f, b.getRight() - 7.5f, b.getY() + 3.5f, 1.4f);
        g.drawLine (b.getRight() - 3.5f, b.getY() + 3.5f, b.getRight() - 3.5f, b.getY() + 7.5f, 1.4f);
    }

    void download (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        auto b = r.withSizeKeepingCentre (10.0f, 12.0f);
        g.setColour (c);
        g.drawLine (b.getCentreX(), b.getY(), b.getCentreX(), b.getBottom() - 3.5f, 1.8f);
        juce::Path head;
        head.addTriangle (b.getX() + 1.0f, b.getBottom() - 6.5f, b.getRight() - 1.0f, b.getBottom() - 6.5f, b.getCentreX(), b.getBottom() - 2.0f);
        g.fillPath (head);
        g.fillRect (b.getX(), b.getBottom() - 1.4f, b.getWidth(), 1.4f);
    }

    void chevron (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool up)
    {
        auto b = r.withSizeKeepingCentre (8.0f, 4.5f);
        juce::Path p;
        if (up) { p.startNewSubPath (b.getX(), b.getBottom()); p.lineTo (b.getCentreX(), b.getY()); p.lineTo (b.getRight(), b.getBottom()); }
        else    { p.startNewSubPath (b.getX(), b.getY()); p.lineTo (b.getCentreX(), b.getBottom()); p.lineTo (b.getRight(), b.getY()); }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    void playMode (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, int mode)
    {
        auto b = r.withSizeKeepingCentre (16.0f, 12.0f);
        g.setColour (c);
        juce::Path p;
        const juce::PathStrokeType st (1.6f);
        auto arrowHead = [&g] (float x, float y, bool right)
        {
            juce::Path h;
            if (right) h.addTriangle (x, y - 3.0f, x, y + 3.0f, x + 4.0f, y);
            else       h.addTriangle (x, y - 3.0f, x, y + 3.0f, x - 4.0f, y);
            g.fillPath (h);
        };

        switch (mode)
        {
            case 0: // one-shot forward  ->|
                g.drawLine (b.getX(), b.getCentreY(), b.getRight() - 6.0f, b.getCentreY(), 1.6f);
                arrowHead (b.getRight() - 6.0f, b.getCentreY(), true);
                g.drawLine (b.getRight(), b.getY(), b.getRight(), b.getBottom(), 1.6f);
                break;
            case 1: // one-shot reverse  |<-
                g.drawLine (b.getX(), b.getY(), b.getX(), b.getBottom(), 1.6f);
                arrowHead (b.getX() + 6.0f, b.getCentreY(), false);
                g.drawLine (b.getX() + 6.0f, b.getCentreY(), b.getRight(), b.getCentreY(), 1.6f);
                break;
            case 2: // loop
                p.startNewSubPath (b.getX() + 2.0f, b.getBottom() - 2.0f);
                p.lineTo (b.getX(), b.getBottom() - 2.0f);
                p.lineTo (b.getX(), b.getY() + 2.0f);
                p.lineTo (b.getRight() - 4.0f, b.getY() + 2.0f);
                p.startNewSubPath (b.getRight() - 2.0f, b.getY() + 2.0f);
                p.lineTo (b.getRight(), b.getY() + 2.0f);
                p.lineTo (b.getRight(), b.getBottom() - 2.0f);
                p.lineTo (b.getX() + 4.0f, b.getBottom() - 2.0f);
                g.strokePath (p, st);
                arrowHead (b.getRight() - 4.0f, b.getY() + 2.0f, true);
                arrowHead (b.getX() + 4.0f, b.getBottom() - 2.0f, false);
                break;
            default: // ping-pong <->
                g.drawLine (b.getX() + 4.0f, b.getCentreY(), b.getRight() - 4.0f, b.getCentreY(), 1.6f);
                arrowHead (b.getX() + 4.0f, b.getCentreY(), false);
                arrowHead (b.getRight() - 4.0f, b.getCentreY(), true);
                break;
        }
    }
}
