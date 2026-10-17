#include "ModulePanels.h"

using namespace RackMetrics;

//==============================================================================
std::unique_ptr<ModulePanel> ModulePanel::create (EditorContext& c, Module& m)
{
    switch (m.type)
    {
        case ModType::Macro:       return std::make_unique<MacroPanel> (c, m);
        case ModType::LFO:         return std::make_unique<LfoPanel> (c, m);
        case ModType::XYPad:       return std::make_unique<XYPanel> (c, m);
        case ModType::Blender:     return std::make_unique<BlenderPanel> (c, m);
        case ModType::EnvFollower: return std::make_unique<EnvPanel> (c, m);
        case ModType::Randomizer:  return std::make_unique<RandomPanel> (c, m);
        case ModType::MidiMacro:   return std::make_unique<MidiMacroPanel> (c, m);
        default: break;
    }
    return {};
}

ModulePanel::ModulePanel (EditorContext& c, Module& m) : module (m), ctx (c)
{
    power.setToggleState (m.enabled.load(), juce::dontSendNotification);
    power.onClick = [this] { module.enabled.store (power.getToggleState()); repaint(); };
    addAndMakeVisible (power);

    closeBtn.setTooltip ("Remove this module");
    closeBtn.onClick = [this] { ctx.deferredRemoveModule (module.uid); };
    addAndMakeVisible (closeBtn);
}

void ModulePanel::addAssignButton (const juce::String& text, int output)
{
    auto b = std::make_unique<AssignButton> (ctx, text);
    b->setTooltip ("Click, then move a control in the plug-in (or click a flashing control in another module).\n"
                   "Right-click to pick a plug-in parameter from a list.");
    auto* raw = b.get();
    raw->onClick = [this, output]
    {
        if (suppressAssignClick) { suppressAssignClick = false; return; }
        ctx.toggleLearn (module, output);
    };
    raw->addMouseListener (this, false);
    addAndMakeVisible (*raw);
    assigns.push_back (std::move (b));
    assignOutputs.push_back (output);
}

ValueBox* ModulePanel::addValueBox (const juce::String& pid, ValueBox::Style s, const juce::String& label)
{
    auto* p = module.getParam (pid);
    jassert (p != nullptr);
    auto vb = std::make_unique<ValueBox> (ctx, module, *p, s, label);
    auto* raw = vb.get();
    addAndMakeVisible (*raw);
    controls.push_back (std::move (vb));
    return raw;
}

ModKnob* ModulePanel::addKnob (const juce::String& pid, bool bipolar, juce::Colour c)
{
    auto* p = module.getParam (pid);
    jassert (p != nullptr);
    auto k = std::make_unique<ModKnob> (ctx, module, *p, bipolar, c);
    auto* raw = k.get();
    addAndMakeVisible (*raw);
    controls.push_back (std::move (k));
    return raw;
}

void ModulePanel::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < assigns.size(); ++i)
    {
        if (e.eventComponent == assigns[i].get())
        {
            suppressAssignClick = e.mods.isPopupMenu();
            if (suppressAssignClick)
                ctx.showHostParamMenu (module, assignOutputs[i]);
            return;
        }
    }
    if (e.eventComponent == this) ctx.cancelLearn();
}

void ModulePanel::paintOverChildren (juce::Graphics& g)
{
    if (! module.enabled.load())
    {
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (getLocalBounds().withTrimmedTop (headerH).toFloat().reduced (1.0f), 8.0f);
    }
}

void ModulePanel::tick()
{
    for (size_t i = 0; i < assigns.size(); ++i)
    {
        const bool l = ctx.isLearningFrom (module.uid, assignOutputs[i]);
        if (l || assigns[i]->learning != l) { assigns[i]->learning = l; assigns[i]->repaint(); }
    }
    for (auto& c : controls) c->repaint();
    for (auto& d : incomingDials) d->repaint();
}

void ModulePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    juce::ColourGradient grad (Theme::panelTop, 0.0f, 0.0f, Theme::panel, 0.0f, (float) getHeight(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    if (nameInHeader() && ! nameArea.isEmpty())
    {
        g.setColour (module.enabled.load() ? Theme::text : Theme::dim);
        g.setFont (Theme::mono (13.0f));
        g.drawText (module.getName(), nameArea, juce::Justification::centredLeft);
    }

    paintBody (g);
}

void ModulePanel::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerH).reduced (8, 5);
    power.setBounds (header.removeFromLeft (16));
    header.removeFromLeft (6);
    closeBtn.setBounds (header.removeFromRight (16));
    header.removeFromRight (4);

    if (nameInHeader())
    {
        const int w = (int) Theme::textWidth (Theme::mono (13.0f), module.getName()) + 12;
        nameArea = header.removeFromLeft (w);
    }

    for (auto& b : assigns)
    {
        const int w = b->getName().length() > 6 ? 62 : 56;
        b->setBounds (header.removeFromLeft (w));
        header.removeFromLeft (4);
    }

    layoutBody (r.reduced (8, 0).withTrimmedBottom (6));
    layoutIncomingDials();
}

void ModulePanel::layoutIncomingDials()
{
    auto& engine = ctx.getProcessor().engine;
    if (! builtIncoming)
    {
        builtIncoming = true;
        for (auto& c : controls)
            for (auto* a : engine.getIncoming (module.uid, c->param.id))
            {
                auto d = std::make_unique<DepthDial> (ctx, a->uid, true);
                d->getProperties().set ("param", c->param.id);
                addAndMakeVisible (*d);
                incomingDials.push_back (std::move (d));
            }
    }

    for (auto& c : controls)
    {
        std::vector<DepthDial*> mine;
        for (auto& d : incomingDials)
            if (d->getProperties()["param"].toString() == c->param.id)
                mine.push_back (d.get());
        if (mine.empty()) continue;

        const int size = 12, gap = 2;
        const int total = (int) mine.size() * (size + gap) - gap;
        int x = c->getBounds().getCentreX() - total / 2;
        const int y = juce::jmin (c->getBottom() + 1, getHeight() - size - 2);
        for (auto* d : mine)
        {
            d->setBounds (x, y, size, size);
            x += size + gap;
        }
    }
}

//==============================================================================
void HistoryView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient bg (juce::Colour (0xff1b2421), 0.0f, 0.0f, juce::Colour (0xff101413), 0.0f, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (r, 4.0f);

    auto a = r.reduced (4.0f, 6.0f);
    const float base = bipolar ? a.getCentreY() : a.getBottom();
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) base, a.getX(), a.getRight());

    auto yFor = [&] (float v)
    {
        return bipolar ? a.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * a.getHeight() * 0.5f
                       : a.getBottom() - juce::jlimit (0.0f, 1.0f, v) * a.getHeight();
    };

    juce::Path line, fill;
    const int n = (int) buf.size();
    for (int i = 0; i < n; ++i)
    {
        const float v = buf[(size_t) ((pos + i) % n)];
        const float x = a.getX() + a.getWidth() * (float) i / (float) (n - 1);
        if (i == 0) { line.startNewSubPath (x, yFor (v)); fill.startNewSubPath (x, base); }
        line.lineTo (x, yFor (v));
        fill.lineTo (x, yFor (v));
    }
    fill.lineTo (a.getRight(), base);
    fill.closeSubPath();

    juce::ColourGradient fg (colour.withAlpha (0.38f), 0.0f, a.getY(), colour.withAlpha (0.03f), 0.0f, base, false);
    g.setGradientFill (fg);
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

    const float last = buf[(size_t) ((pos + n - 1) % n)];
    g.fillEllipse (a.getRight() - 3.5f, yFor (last) - 3.5f, 7.0f, 7.0f);
}

//==============================================================================
LfoCurveView::LfoCurveView (EditorContext& c, LfoModule& l) : ctx (c), lfo (l)
{
    setRepaintsOnMouseActivity (true);
}

int LfoCurveView::hitNode (juce::Point<float> p) const
{
    const auto& c = lfo.curve;
    int best = -1; float bestD = 8.0f;
    for (int i = 0; i < (int) c.nodes.size(); ++i)
    {
        const float d = p.getDistanceFrom ({ toX (c.nodes[(size_t) i].x), toY (c.nodes[(size_t) i].y) });
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int LfoCurveView::hitSegment (juce::Point<float> p) const
{
    const auto& c = lfo.curve;
    const float x = fromX (p.x);
    const int s = c.segmentAt (x);
    if (s < 0) return -1;
    return std::abs (toY (c.evaluate (x)) - p.y) < 10.0f ? s : -1;
}

void LfoCurveView::apply (const Curve& c)
{
    {
        const juce::ScopedLock sl (ctx.getProcessor().engine.lock);
        lfo.curve = c;
        lfo.presetIndex = -1;
    }
    repaint();
    if (onEdited) onEdited();
}

void LfoCurveView::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    juce::ColourGradient bg (juce::Colour (0xff221d2c), 0.0f, 0.0f, juce::Colour (0xff111016), 0.0f, full.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (full, 4.0f);

    const auto a = area();
    const auto& c = lfo.curve;
    const auto col = Theme::lfo;

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());

    // phase start line
    const float phase = lfo.params[1]->getEffReal() / 360.0f;
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawVerticalLine ((int) toX (phase), a.getY() - 2.0f, a.getBottom() + 2.0f);

    juce::Path line, fill;
    const int n = juce::jmax (64, (int) a.getWidth());
    for (int i = 0; i <= n; ++i)
    {
        const float x = (float) i / (float) n;
        const float px = toX (x), py = toY (c.evaluate (x));
        if (i == 0) { line.startNewSubPath (px, py); fill.startNewSubPath (px, a.getCentreY()); }
        line.lineTo (px, py);
        fill.lineTo (px, py);
    }
    fill.lineTo (toX (1.0f), a.getCentreY());
    fill.closeSubPath();

    juce::ColourGradient fg (col.withAlpha (0.5f), 0.0f, a.getY(), col.withAlpha (0.5f), 0.0f, a.getBottom(), false);
    fg.addColour (0.5, col.withAlpha (0.04f));
    g.setGradientFill (fg);
    g.fillPath (fill);
    g.setColour (col);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // highlight the segment under the mouse
    const int hs = dragSeg >= 0 ? dragSeg : hoverSeg;
    if (hs >= 0 && hs + 1 < (int) c.nodes.size())
    {
        juce::Path seg;
        const float x0 = c.nodes[(size_t) hs].x, x1 = c.nodes[(size_t) hs + 1].x;
        for (int i = 0; i <= 40; ++i)
        {
            const float x = x0 + (x1 - x0) * (float) i / 40.0f;
            const float y = c.nodes[(size_t) hs].y + (c.nodes[(size_t) hs + 1].y - c.nodes[(size_t) hs].y)
                                                     * Curve::segShape ((float) i / 40.0f, c.segs[(size_t) hs]);
            if (i == 0) seg.startNewSubPath (toX (x), toY (y)); else seg.lineTo (toX (x), toY (y));
        }
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.strokePath (seg, juce::PathStrokeType (2.0f));
    }

    // nodes
    const bool showNodes = isMouseOver() || dragNode >= 0;
    for (int i = 0; i < (int) c.nodes.size(); ++i)
    {
        const bool hot = i == hoverNode || i == dragNode;
        if (! showNodes && ! hot) continue;
        const float r = hot ? 5.0f : 3.5f;
        g.setColour (hot ? juce::Colours::white : col.brighter (0.4f));
        g.fillEllipse (toX (c.nodes[(size_t) i].x) - r, toY (c.nodes[(size_t) i].y) - r, r * 2.0f, r * 2.0f);
    }

    // playhead
    if (lfo.enabled.load())
    {
        const float p = lfo.uiPos.load();
        const float px = toX (p), py = toY (c.evaluate (p));
        g.setColour (col.withAlpha (0.35f));
        g.fillEllipse (px - 7.0f, py - 7.0f, 14.0f, 14.0f);
        g.setColour (col.brighter (0.5f));
        g.fillEllipse (px - 4.5f, py - 4.5f, 9.0f, 9.0f);
    }
}

void LfoCurveView::mouseMove (const juce::MouseEvent& e)
{
    const int n = hitNode (e.position);
    const int s = n < 0 ? hitSegment (e.position) : -1;
    if (n != hoverNode || s != hoverSeg) { hoverNode = n; hoverSeg = s; repaint(); }
    setMouseCursor (n >= 0 ? juce::MouseCursor::DraggingHandCursor
                           : s >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void LfoCurveView::mouseExit (const juce::MouseEvent&)
{
    hoverNode = hoverSeg = -1;
    repaint();
}

void LfoCurveView::mouseDown (const juce::MouseEvent& e)
{
    if (ctx.isLearning()) { ctx.cancelLearn(); return; }

    dragNode = dragSeg = pairedNode = -1;
    const auto& c = lfo.curve;

    if (e.mods.isPopupMenu())
    {
        const int n = hitNode (e.position);
        if (n >= 0) { editNodeDialog (n); return; }
        const int s = hitSegment (e.position);
        if (s >= 0) editSegmentDialog (s);
        return;
    }

    dragNode = hitNode (e.position);
    if (dragNode >= 0)
    {
        nodeStartY = c.nodes[(size_t) dragNode].y;
        if (e.mods.isShiftDown() && c.nodes.size() > 1)
        {
            pairedNode = dragNode + 1 < (int) c.nodes.size() ? dragNode + 1 : dragNode - 1;
            pairedStartY = c.nodes[(size_t) pairedNode].y;
        }
        return;
    }

    dragSeg = hitSegment (e.position);
    if (dragSeg >= 0)
    {
        const auto& n0 = c.nodes[(size_t) dragSeg];
        const auto& n1 = c.nodes[(size_t) dragSeg + 1];
        segStartBend = c.segs[(size_t) dragSeg].bend;
        segPosAtDown = juce::jlimit (0.05f, 0.95f, (fromX (e.position.x) - n0.x) / juce::jmax (1.0e-4f, n1.x - n0.x));
        segPosSet = false;
    }
}

void LfoCurveView::mouseDrag (const juce::MouseEvent& e)
{
    Curve c = lfo.curve;

    if (dragNode >= 0 && dragNode < (int) c.nodes.size())
    {
        const int last = (int) c.nodes.size() - 1;
        float x = fromX (e.position.x);
        const float y = fromY (e.position.y);
        if (dragNode == 0) x = 0.0f;
        else if (dragNode == last) x = 1.0f;
        else x = juce::jlimit (c.nodes[(size_t) dragNode - 1].x, c.nodes[(size_t) dragNode + 1].x, x);
        c.nodes[(size_t) dragNode] = { x, y };

        if (juce::isPositiveAndBelow (pairedNode, (int) c.nodes.size()))
            c.nodes[(size_t) pairedNode].y = juce::jlimit (-1.0f, 1.0f, pairedStartY + (y - nodeStartY));

        apply (c);
        return;
    }

    if (dragSeg >= 0 && dragSeg < (int) c.segs.size())
    {
        auto& s = c.segs[(size_t) dragSeg];
        if (! segPosSet && std::abs (segStartBend) < 0.02f)
            s.pos = segPosAtDown;      // bend around the point that was grabbed
        segPosSet = true;

        const float dy = c.nodes[(size_t) dragSeg + 1].y - c.nodes[(size_t) dragSeg].y;
        const float dir = dy >= 0.0f ? 1.0f : -1.0f;
        s.bend = juce::jlimit (-1.0f, 1.0f, segStartBend - (float) e.getDistanceFromDragStartY() / 50.0f * dir);
        apply (c);
    }
}

void LfoCurveView::mouseUp (const juce::MouseEvent&)
{
    dragNode = dragSeg = pairedNode = -1;
    repaint();
}

void LfoCurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    Curve c = lfo.curve;
    const int n = hitNode (e.position);
    if (n >= 0)
    {
        if (c.removeNode (n)) apply (c);
        return;
    }
    c.addNode (fromX (e.position.x), fromY (e.position.y));
    apply (c);
}

void LfoCurveView::editNodeDialog (int n)
{
    const auto node = lfo.curve.nodes[(size_t) n];
    auto* aw = new juce::AlertWindow ("Node " + juce::String (n + 1), "Exact time (0 - 1) and amplitude (-1 to +1)",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("t", juce::String (node.x, 4), "Time");
    aw->addTextEditor ("a", juce::String (node.y, 4), "Amplitude");
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LfoCurveView> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, n] (int r)
    {
        if (r != 1 || safe == nullptr) return;
        Curve c = safe->lfo.curve;
        if (! juce::isPositiveAndBelow (n, (int) c.nodes.size())) return;
        const int last = (int) c.nodes.size() - 1;
        float x = aw->getTextEditorContents ("t").getFloatValue();
        if (n == 0) x = 0.0f;
        else if (n == last) x = 1.0f;
        else x = juce::jlimit (c.nodes[(size_t) n - 1].x, c.nodes[(size_t) n + 1].x, x);
        c.nodes[(size_t) n] = { x, juce::jlimit (-1.0f, 1.0f, aw->getTextEditorContents ("a").getFloatValue()) };
        safe->apply (c);
    }), true);
}

void LfoCurveView::editSegmentDialog (int s)
{
    const auto seg = lfo.curve.segs[(size_t) s];
    auto* aw = new juce::AlertWindow ("Curve " + juce::String (s + 1), "Bend (-100 to +100) and where the bend is centred (0 - 100 %)",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("b", juce::String (seg.bend * 100.0f, 1), "Bend");
    aw->addTextEditor ("p", juce::String (seg.pos * 100.0f, 1), "Position %");
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LfoCurveView> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, s] (int r)
    {
        if (r != 1 || safe == nullptr) return;
        Curve c = safe->lfo.curve;
        if (! juce::isPositiveAndBelow (s, (int) c.segs.size())) return;
        c.segs[(size_t) s].bend = juce::jlimit (-1.0f, 1.0f, aw->getTextEditorContents ("b").getFloatValue() / 100.0f);
        c.segs[(size_t) s].pos = juce::jlimit (0.01f, 0.99f, aw->getTextEditorContents ("p").getFloatValue() / 100.0f);
        safe->apply (c);
    }), true);
}

//==============================================================================
XYPadView::XYPadView (EditorContext& c, XYModule& m) : ctx (c), xy (m)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void XYPadView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r, 5.0f);
        g.reduceClipRegion (clip);
        const int cells = 4;
        for (int i = 0; i < cells; ++i)
            for (int j = 0; j < cells; ++j)
            {
                g.setColour (((i + j) % 2 == 0) ? juce::Colour (0xff151519) : juce::Colour (0xff202026));
                g.fillRect (r.getX() + r.getWidth() * (float) i / cells, r.getY() + r.getHeight() * (float) j / cells,
                            r.getWidth() / cells + 1.0f, r.getHeight() / cells + 1.0f);
            }
    }
    g.setColour (Theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);

    auto posFor = [&] (float x, float y)
    {
        return juce::Point<float> (r.getX() + (x + 1.0f) * 0.5f * r.getWidth(), r.getBottom() - (y + 1.0f) * 0.5f * r.getHeight());
    };

    const auto base = posFor (xy.params[0]->getBaseReal(), xy.params[1]->getBaseReal());
    const auto eff  = posFor (xy.params[0]->getEffReal(),  xy.params[1]->getEffReal());

    if (base.getDistanceFrom (eff) > 1.5f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawEllipse (base.x - 6.0f, base.y - 6.0f, 12.0f, 12.0f, 1.2f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillEllipse (eff.x - 13.0f, eff.y - 13.0f, 26.0f, 26.0f);
    juce::ColourGradient grad (juce::Colours::white, eff.x - 3.0f, eff.y - 3.0f, juce::Colour (0xffb8b8c0), eff.x + 6.0f, eff.y + 6.0f, true);
    g.setGradientFill (grad);
    g.fillEllipse (eff.x - 7.5f, eff.y - 7.5f, 15.0f, 15.0f);
}

void XYPadView::setFrom (juce::Point<float> p)
{
    auto r = getLocalBounds().toFloat();
    const float x = juce::jlimit (-1.0f, 1.0f, (p.x - r.getX()) / r.getWidth() * 2.0f - 1.0f);
    const float y = juce::jlimit (-1.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight() * 2.0f - 1.0f);
    xy.params[0]->setBaseNorm (xy.params[0]->range.convertTo0to1 (x));
    xy.params[1]->setBaseNorm (xy.params[1]->range.convertTo0to1 (y));
    repaint();
}

void XYPadView::mouseDown (const juce::MouseEvent& e)
{
    if (ctx.isLearning()) { ctx.cancelLearn(); return; }
    dragging = true;
    xy.params[0]->beginGesture();
    xy.params[1]->beginGesture();
    setFrom (e.position);
}

void XYPadView::mouseDrag (const juce::MouseEvent& e) { if (dragging) setFrom (e.position); }

void XYPadView::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    xy.params[0]->endGesture();
    xy.params[1]->endGesture();
}

void XYPadView::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto& p : xy.params) { p->beginGesture(); p->setBaseNorm (p->range.convertTo0to1 (0.0f)); p->endGesture(); }
    repaint();
}

//==============================================================================
BlendGraphView::BlendGraphView (EditorContext& c, BlenderModule& b) : ctx (c), bl (b)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void BlendGraphView::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float R = ringRadius();
    const float outer = R + 14.0f;

    juce::ColourGradient disc (juce::Colour (0xff20232c), c.x, c.y - outer * 0.4f, juce::Colour (0xff0c0d10), c.x, c.y + outer, true);
    g.setGradientFill (disc);
    g.fillEllipse (c.x - outer, c.y - outer, outer * 2.0f, outer * 2.0f);
    g.setColour (juce::Colour (0xff2c2f38));
    g.drawEllipse (c.x - outer, c.y - outer, outer * 2.0f, outer * 2.0f, 1.0f);

    g.setColour (Theme::blend.withAlpha (0.08f));
    g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    g.setColour (Theme::blend.withAlpha (0.75f));
    g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.6f);

    const float sx[4] = { 0.0f, 1.0f, 0.0f, -1.0f };
    const float sy[4] = { -1.0f, 0.0f, 1.0f, 0.0f };
    g.setFont (Theme::mono (12.0f));
    for (int i = 0; i < 4; ++i)
    {
        const float w = bl.uiWeights[(size_t) i].load();
        const bool filled = bl.slots[(size_t) i].filled();
        const juce::Point<float> lp (c.x + sx[i] * (R + 8.0f), c.y + sy[i] * (R + 8.0f));
        g.setColour (filled ? Theme::text.withAlpha (0.4f + 0.6f * w) : Theme::faint);
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (14.0f, 14.0f).withCentre (lp), juce::Justification::centred);

        if (filled && w > 0.01f)
        {
            const juce::Point<float> dp (c.x + sx[i] * R, c.y + sy[i] * R);
            const float d = 3.0f + 6.0f * w;
            g.setColour (Theme::blend.withAlpha (0.3f + 0.7f * w));
            g.fillEllipse (dp.x - d * 0.5f, dp.y - d * 0.5f, d, d);
        }
    }

    const float range = bl.params[0]->getEffReal();
    const float ang = juce::degreesToRadians (bl.params[1]->getEffReal());
    const juce::Point<float> np (c.x + std::sin (ang) * range * R, c.y - std::cos (ang) * range * R);
    g.setColour (Theme::pink.withAlpha (0.3f));
    g.fillEllipse (np.x - 10.0f, np.y - 10.0f, 20.0f, 20.0f);
    g.setColour (Theme::pink);
    g.fillEllipse (np.x - 6.5f, np.y - 6.5f, 13.0f, 13.0f);
}

void BlendGraphView::setFrom (juce::Point<float> p)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float dx = p.x - c.x, dy = p.y - c.y;
    const float r = juce::jlimit (0.0f, 1.0f, std::sqrt (dx * dx + dy * dy) / ringRadius());
    float a = juce::radiansToDegrees (std::atan2 (dx, -dy));
    if (a < 0.0f) a += 360.0f;
    bl.params[0]->setBaseNorm (bl.params[0]->range.convertTo0to1 (r));
    bl.params[1]->setBaseNorm (bl.params[1]->range.convertTo0to1 (a));
    repaint();
}

void BlendGraphView::mouseDown (const juce::MouseEvent& e)
{
    if (ctx.isLearning()) { ctx.cancelLearn(); return; }
    dragging = true;
    bl.params[0]->beginGesture();
    bl.params[1]->beginGesture();
    setFrom (e.position);
}

void BlendGraphView::mouseDrag (const juce::MouseEvent& e) { if (dragging) setFrom (e.position); }

void BlendGraphView::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    bl.params[0]->endGesture();
    bl.params[1]->endGesture();
}

void BlendGraphView::mouseDoubleClick (const juce::MouseEvent&)
{
    bl.params[0]->beginGesture();
    bl.params[0]->setBaseNorm (0.0f);
    bl.params[0]->endGesture();
    repaint();
}

//==============================================================================
MacroPanel::MacroPanel (EditorContext& c, Module& m) : ModulePanel (c, m)
{
    addAssignButton ("ASSIGN", 0);
    knob = addKnob ("value", true, Theme::macro);
    knob->setTooltip ("Drag up/down. Bipolar: -100% to +100%");
}

void MacroPanel::layoutBody (juce::Rectangle<int> r)
{
    nameArea = r.removeFromBottom (18);
    knob->setBounds (r.withSizeKeepingCentre (86, 86).withY (r.getY() + 4));
}

void MacroPanel::paintBody (juce::Graphics& g)
{
    g.setColour (Theme::text);
    g.setFont (Theme::mono (13.0f));
    g.drawText (knob->isDragging() || knob->isMouseOver() ? knob->getDisplayText() : module.getName(),
                nameArea, juce::Justification::centred);
}

void MacroPanel::tick()
{
    ModulePanel::tick();
    repaint (nameArea);
}

//==============================================================================
LfoPanel::LfoPanel (EditorContext& c, Module& m)
    : ModulePanel (c, m), lfo (static_cast<LfoModule&> (m)), view (c, lfo),
      up ("up", [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col) { Icons::chevron (g, r, col, true); }),
      down ("down", [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col) { Icons::chevron (g, r, col, false); }),
      modeBtn ("mode", [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col) { Icons::playMode (g, r, col, lfo.playMode.load()); })
{
    addAssignButton ("ASSIGN", 0);
    addAndMakeVisible (view);
    view.onEdited = [this] { refreshShapeBox(); };
    view.setTooltip ("Drag nodes / curves. Double-click: add or remove a node. Right-click: exact values. Shift: move two nodes.");

    auto names = Curve::presetNames();
    for (int i = 0; i < names.size(); ++i) shape.addItem (names[i], i + 1);
    shape.addItem ("Custom", 100);
    shape.setItemEnabled (100, false);
    shape.onChange = [this] { const int id = shape.getSelectedId(); if (id > 0 && id < 100) selectPreset (id - 1); };
    addAndMakeVisible (shape);
    refreshShapeBox();

    up.onClick = [this] { auto n = Curve::presetNames().size(); selectPreset (lfo.presetIndex < 0 ? 0 : (lfo.presetIndex + n - 1) % n); };
    down.onClick = [this] { auto n = Curve::presetNames().size(); selectPreset (lfo.presetIndex < 0 ? 0 : (lfo.presetIndex + 1) % n); };
    up.drawBackground = down.drawBackground = true;
    addAndMakeVisible (up);
    addAndMakeVisible (down);

    phase = addValueBox ("phase", ValueBox::Style::Labelled);
    rate = addValueBox ("rate", ValueBox::Style::Boxed);
    rate->setTooltip ("Rate - Hz, or a note value when synced to the DAW tempo");

    syncBox.addItem ("Hz", 1);
    syncBox.addItem ("Sync", 2);
    syncBox.setSelectedId (lfo.tempoSync.load() ? 2 : 1, juce::dontSendNotification);
    syncBox.onChange = [this] { lfo.tempoSync.store (syncBox.getSelectedId() == 2); rate->repaint(); };
    syncBox.setTooltip ("Free-running in Hz, or locked to the DAW tempo");
    addAndMakeVisible (syncBox);

    modeBtn.drawBackground = true;
    modeBtn.colour = Theme::value;
    modeBtn.setTooltip ("Playback mode");
    modeBtn.onClick = [this]
    {
        juce::PopupMenu m;
        const char* names2[] = { "One-Shot Forward", "One-Shot Reverse", "Loop", "PingPong" };
        for (int i = 0; i < 4; ++i)
            m.addItem (i + 1, names2[i], true, lfo.playMode.load() == i);
        m.addSeparator();
        m.addItem (10, "Retrigger now");
        juce::Component::SafePointer<LfoPanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modeBtn), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 10) safe->lfo.retriggerRequest.store (true);
            else safe->lfo.playMode.store (r - 1);
            safe->modeBtn.repaint();
        });
    };
    addAndMakeVisible (modeBtn);
}

void LfoPanel::selectPreset (int i)
{
    {
        const juce::ScopedLock sl (ctx.getProcessor().engine.lock);
        lfo.curve = Curve::makePreset (i);
        lfo.presetIndex = i;
    }
    refreshShapeBox();
    view.repaint();
}

void LfoPanel::refreshShapeBox()
{
    shape.setSelectedId (lfo.presetIndex >= 0 ? lfo.presetIndex + 1 : 100, juce::dontSendNotification);
}

void LfoPanel::layoutBody (juce::Rectangle<int> r)
{
    view.setBounds (r.removeFromTop (92));
    r.removeFromTop (4);
    auto row = r.removeFromTop (26);
    shape.setBounds (row.removeFromLeft (92));
    row.removeFromLeft (2);
    auto ud = row.removeFromLeft (15);
    up.setBounds (ud.removeFromTop (13));
    down.setBounds (ud.withTrimmedTop (1));
    row.removeFromLeft (4);
    phase->setBounds (row.removeFromLeft (48));
    row.removeFromLeft (4);
    rate->setBounds (row.removeFromLeft (64));
    row.removeFromLeft (4);
    syncBox.setBounds (row.removeFromLeft (54));
    row.removeFromLeft (4);
    modeBtn.setBounds (row.removeFromLeft (30));
}

void LfoPanel::tick()
{
    ModulePanel::tick();
    view.repaint();
}

//==============================================================================
XYPanel::XYPanel (EditorContext& c, Module& m) : ModulePanel (c, m), pad (c, static_cast<XYModule&> (m))
{
    addAssignButton ("ASGN X", 0);
    addAssignButton ("ASGN Y", 1);
    addAndMakeVisible (pad);
    pad.setTooltip ("Drag the node. Double-click to centre.");
    addValueBox ("x", ValueBox::Style::Boxed);
    addValueBox ("y", ValueBox::Style::Boxed);
}

void XYPanel::layoutBody (juce::Rectangle<int> r)
{
    pad.setBounds (r.removeFromTop (84).withSizeKeepingCentre (juce::jmin (r.getWidth(), 150), 84));
    nameArea = r.removeFromTop (14);
    r.removeFromTop (2);
    auto row = r.removeFromTop (22).withSizeKeepingCentre (152, 22);
    xLabel = row.removeFromLeft (14);
    controls[0]->setBounds (row.removeFromLeft (58));
    row.removeFromLeft (8);
    yLabel = row.removeFromLeft (14);
    controls[1]->setBounds (row.removeFromLeft (58));
}

void XYPanel::paintBody (juce::Graphics& g)
{
    g.setFont (Theme::mono (12.0f));
    g.setColour (Theme::value);
    g.drawText ("X", xLabel, juce::Justification::centredLeft);
    g.drawText ("Y", yLabel, juce::Justification::centredLeft);
    g.setColour (Theme::dim);
    g.setFont (Theme::mono (11.0f));
    g.drawText (module.getName(), nameArea, juce::Justification::centred);
}

void XYPanel::tick()
{
    ModulePanel::tick();
    pad.repaint();
}

//==============================================================================
BlenderPanel::BlenderPanel (EditorContext& c, Module& m)
    : ModulePanel (c, m), bl (static_cast<BlenderModule&> (m)), graph (c, bl)
{
    addAndMakeVisible (graph);
    graph.setTooltip ("Move the node towards a number to hear more of that preset. Double-click to centre.");

    for (int i = 0; i < 4; ++i)
    {
        auto& cb = slotBoxes[(size_t) i];
        cb.onBeforePopup = [this, i] { fillSlotMenu (i); };
        cb.onChange = [this, i]
        {
            const int id = slotBoxes[(size_t) i].getSelectedId();
            auto& proc = ctx.getProcessor();
            if (id == 2) proc.captureSnapshot (bl.uid, i);
            else if (id == 3) proc.clearBlendSlot (bl.uid, i);
            else if (id >= 100) proc.captureProgram (bl.uid, i, id - 100);
            juce::Component::SafePointer<BlenderPanel> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->refreshSlots(); });
        };
        cb.setTooltip ("Pick a factory preset of the loaded plug-in, or capture its current state");
        addAndMakeVisible (cb);

        captureBtns[(size_t) i] = std::make_unique<PathButton> ("capture", Icons::download);
        captureBtns[(size_t) i]->drawBackground = true;
        captureBtns[(size_t) i]->colour = Theme::value;
        captureBtns[(size_t) i]->setTooltip ("Snapshot the plug-in's current settings into slot " + juce::String (i + 1));
        captureBtns[(size_t) i]->onClick = [this, i] { ctx.getProcessor().captureSnapshot (bl.uid, i); refreshSlots(); };
        addAndMakeVisible (*captureBtns[(size_t) i]);
    }

    addValueBox ("range", ValueBox::Style::Boxed);
    addValueBox ("angle", ValueBox::Style::Boxed);
    refreshSlots();
}

void BlenderPanel::fillSlotMenu (int slot)
{
    auto& cb = slotBoxes[(size_t) slot];
    cb.clear (juce::dontSendNotification);
    cb.addItem (bl.slots[(size_t) slot].name, 1);
    cb.addSeparator();
    cb.addItem ("Capture current state", 2);
    cb.addItem ("Clear slot", 3);
    cb.setItemEnabled (3, bl.slots[(size_t) slot].filled());
    cb.addSectionHeading ("Factory presets");
    auto names = ctx.getProcessor().getHostedProgramNames();
    if (names.isEmpty())
    {
        cb.addItem ("(this plug-in doesn't share its presets)", 4);
        cb.setItemEnabled (4, false);
    }
    for (int i = 0; i < names.size(); ++i)
        cb.addItem (juce::String (i + 1) + ". " + names[i], 100 + i);
    cb.setSelectedId (1, juce::dontSendNotification);
}

void BlenderPanel::refreshSlots()
{
    lastSlotsVersion = bl.slotsVersion;
    for (int i = 0; i < 4; ++i)
    {
        auto& cb = slotBoxes[(size_t) i];
        cb.clear (juce::dontSendNotification);
        cb.addItem (bl.slots[(size_t) i].name, 1);
        cb.setSelectedId (1, juce::dontSendNotification);
        cb.setColour (juce::ComboBox::textColourId, bl.slots[(size_t) i].filled() ? Theme::text : Theme::dim);
    }
    repaint();
}

void BlenderPanel::layoutBody (juce::Rectangle<int> r)
{
    graph.setBounds (r.removeFromLeft (136).withSizeKeepingCentre (136, 136));
    r.removeFromLeft (6);
    auto slotsArea = r.removeFromTop (100).withTrimmedTop (2);
    for (int i = 0; i < 4; ++i)
    {
        auto row = slotsArea.removeFromTop (25).withTrimmedBottom (3);
        numberAreas[(size_t) i] = row.removeFromLeft (12);
        row.removeFromLeft (3);
        captureBtns[(size_t) i]->setBounds (row.removeFromRight (22));
        row.removeFromRight (4);
        slotBoxes[(size_t) i].setBounds (row);
    }
    r.removeFromTop (6);
    auto row = r.removeFromTop (22);
    rangeLabel = row.removeFromLeft (44);
    controls[0]->setBounds (row.removeFromLeft (54));
    row.removeFromLeft (10);
    angleLabel = row.removeFromLeft (44);
    controls[1]->setBounds (row.removeFromLeft (54));
}

void BlenderPanel::paintBody (juce::Graphics& g)
{
    g.setFont (Theme::mono (12.0f));
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (bl.slots[(size_t) i].filled() ? Theme::text : Theme::faint);
        g.drawText (juce::String (i + 1), numberAreas[(size_t) i], juce::Justification::centred);
    }
    g.setColour (Theme::value);
    g.drawText ("Range", rangeLabel, juce::Justification::centredLeft);
    g.drawText ("Angle", angleLabel, juce::Justification::centredLeft);
}

void BlenderPanel::tick()
{
    ModulePanel::tick();
    graph.repaint();
    if (bl.slotsVersion != lastSlotsVersion && ! slotBoxes[0].isPopupActive() && ! slotBoxes[1].isPopupActive()
         && ! slotBoxes[2].isPopupActive() && ! slotBoxes[3].isPopupActive())
        refreshSlots();
}

//==============================================================================
EnvPanel::EnvPanel (EditorContext& c, Module& m) : ModulePanel (c, m), env (static_cast<EnvFollowerModule&> (m))
{
    addAssignButton ("ASSIGN", 0);
    addAndMakeVisible (hist);
    addValueBox ("input", ValueBox::Style::Labelled);
    addValueBox ("attack", ValueBox::Style::Labelled);
    addValueBox ("release", ValueBox::Style::Labelled);

    modeBox.addItem ("Peak", 1);
    modeBox.addItem ("RMS", 2);
    modeBox.setSelectedId (env.rmsMode.load() ? 2 : 1, juce::dontSendNotification);
    modeBox.onChange = [this] { env.rmsMode.store (modeBox.getSelectedId() == 2); };
    modeBox.setTooltip ("Peak follows fast transients; RMS gives a smoother, averaged response");
    addAndMakeVisible (modeBox);

    auto updateSrc = [this] { srcBtn.setButtonText (env.followOutput.load() ? "OUT" : "IN"); };
    updateSrc();
    srcBtn.setTooltip ("Follow the audio going INTO the plug-in, or what comes OUT of it");
    srcBtn.onClick = [this, updateSrc] { env.followOutput.store (! env.followOutput.load()); updateSrc(); };
    addAndMakeVisible (srcBtn);
}

void EnvPanel::layoutBody (juce::Rectangle<int> r)
{
    hist.setBounds (r.removeFromTop (92));
    r.removeFromTop (4);
    auto row = r.removeFromTop (26);
    controls[0]->setBounds (row.removeFromLeft (52));
    row.removeFromLeft (4);
    controls[1]->setBounds (row.removeFromLeft (60));
    row.removeFromLeft (4);
    controls[2]->setBounds (row.removeFromLeft (64));
    row.removeFromLeft (6);
    modeBox.setBounds (row.removeFromLeft (64));
    row.removeFromLeft (4);
    srcBtn.setBounds (row.removeFromLeft (40));
}

void EnvPanel::tick()
{
    ModulePanel::tick();
    hist.push (module.uiOut[0].load());
}

//==============================================================================
RandomPanel::RandomPanel (EditorContext& c, Module& m) : ModulePanel (c, m), rnd (static_cast<RandomModule&> (m))
{
    addAssignButton ("ASSIGN", 0);
    addAndMakeVisible (hist);
    addValueBox ("amount", ValueBox::Style::Labelled);
    addValueBox ("smooth", ValueBox::Style::Labelled);
    addValueBox ("jitter", ValueBox::Style::Labelled);
    addValueBox ("rate", ValueBox::Style::Boxed);

    syncBox.addItem ("Hz", 1);
    syncBox.addItem ("Sync", 2);
    syncBox.setSelectedId (rnd.tempoSync.load() ? 2 : 1, juce::dontSendNotification);
    syncBox.onChange = [this] { rnd.tempoSync.store (syncBox.getSelectedId() == 2); controls[3]->repaint(); };
    addAndMakeVisible (syncBox);
}

void RandomPanel::layoutBody (juce::Rectangle<int> r)
{
    hist.setBounds (r.removeFromTop (92));
    r.removeFromTop (4);
    auto row = r.removeFromTop (26);
    for (int i = 0; i < 3; ++i)
    {
        controls[(size_t) i]->setBounds (row.removeFromLeft (56));
        row.removeFromLeft (3);
    }
    row.removeFromLeft (4);
    controls[3]->setBounds (row.removeFromLeft (66));
    row.removeFromLeft (4);
    syncBox.setBounds (row.removeFromLeft (56));
}

void RandomPanel::tick()
{
    ModulePanel::tick();
    hist.push (module.uiOut[0].load());
}

//==============================================================================
MidiMacroPanel::MidiMacroPanel (EditorContext& c, Module& m)
    : ModulePanel (c, m), midi (static_cast<MidiMacroModule&> (m)), learnBtn (c, "LEARN")
{
    addAssignButton ("ASSIGN", 0);
    knob = addKnob ("value", false, Theme::green);
    knob->setTooltip ("Incoming MIDI CC value (0-127)");
    learnBtn.colour = Theme::green;
    learnBtn.setTooltip ("Click, then move a knob or fader on your MIDI controller");
    learnBtn.onClick = [this] { midi.learning.store (! midi.learning.load()); };
    addAndMakeVisible (learnBtn);
}

void MidiMacroPanel::layoutBody (juce::Rectangle<int> r)
{
    knob->setBounds (r.removeFromTop (70).withSizeKeepingCentre (68, 68));
    r.removeFromTop (14);
    learnBtn.setBounds (r.removeFromTop (22).withSizeKeepingCentre (76, 22));
    nameArea = r.reduced (0, 2);
}

void MidiMacroPanel::paintBody (juce::Graphics& g)
{
    auto a = nameArea;
    g.setColour (Theme::text);
    g.setFont (Theme::mono (12.5f));
    g.drawText (knob->isDragging() || knob->isMouseOver() ? knob->getDisplayText() : module.getName(),
                a.removeFromTop (a.getHeight() / 2), juce::Justification::centred);
    const int cc = midi.ccNumber.load();
    g.setColour (Theme::dim);
    g.setFont (Theme::mono (10.5f, false));
    g.drawText (midi.learning.load() ? "waiting..." : cc >= 0 ? "CC " + juce::String (cc) : "no CC", a, juce::Justification::centred);
}

void MidiMacroPanel::tick()
{
    ModulePanel::tick();
    learnBtn.learning = midi.learning.load();
    learnBtn.repaint();
    repaint (nameArea);
}
