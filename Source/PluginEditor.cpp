#include "PluginEditor.h"

using namespace RackMetrics;

//==============================================================================
void Logo::paint (juce::Graphics& g)
{
    const auto font = Theme::sans (h, true).withExtraKerningFactor (0.04f);
    const juce::String left = "BIGM", right = "D";
    const float wl = Theme::textWidth (font, left), wr = Theme::textWidth (font, right);
    const float ring = h * 0.92f;
    const float total = wl + ring + 4.0f + wr;
    float x = ((float) getWidth() - total) * 0.5f;
    const float cy = (float) getHeight() * 0.5f;

    g.setFont (font);
    g.setColour (juce::Colours::white);
    g.drawText (left, juce::Rectangle<float> (x, 0.0f, wl + 2.0f, (float) getHeight()), juce::Justification::centredLeft);
    x += wl + 2.0f;

    auto rr = juce::Rectangle<float> (x, cy - ring * 0.5f, ring, ring);
    juce::ColourGradient grad (Theme::logoA, rr.getRight(), rr.getY(), Theme::logoB, rr.getX(), rr.getBottom(), false);
    g.setGradientFill (grad);
    g.drawEllipse (rr.reduced (1.5f), 2.6f);

    juce::Path sine;
    auto inner = rr.reduced (ring * 0.26f, ring * 0.32f);
    for (int i = 0; i <= 30; ++i)
    {
        const float t = (float) i / 30.0f;
        const float px = inner.getX() + t * inner.getWidth();
        const float py = inner.getCentreY() - std::sin (t * juce::MathConstants<float>::twoPi * 1.5f) * inner.getHeight() * 0.5f;
        if (i == 0) sine.startNewSubPath (px, py); else sine.lineTo (px, py);
    }
    g.setColour (Theme::blend);
    g.strokePath (sine, juce::PathStrokeType (1.3f));

    x += ring + 2.0f;
    g.setColour (juce::Colours::white);
    g.drawText (right, juce::Rectangle<float> (x, 0.0f, wr + 4.0f, (float) getHeight()), juce::Justification::centredLeft);
}

//==============================================================================
DialColumn::DialColumn (EditorContext& c, Module& m, int output, juce::String h) : header (std::move (h))
{
    for (auto* a : c.getProcessor().engine.getOutgoing (m.uid, output))
    {
        auto d = std::make_unique<DepthDial> (c, a->uid, false);
        addAndMakeVisible (*d);
        dials.push_back (std::move (d));
    }
}

int DialColumn::getColumnWidth() const
{
    const int cols = juce::jmax (1, ((int) dials.size() + perColumn - 1) / perColumn);
    return 10 + cols * (dialSize + 4) - 4;
}

int DialColumn::getPreferredHeight() const
{
    const int rows = juce::jlimit (1, perColumn, (int) dials.size());
    return (header.isNotEmpty() ? 18 : 6) + rows * (dialSize + 5) + 4;
}

void DialColumn::resized()
{
    const int top = header.isNotEmpty() ? 18 : 6;
    for (size_t i = 0; i < dials.size(); ++i)
    {
        const int col = (int) i / perColumn, row = (int) i % perColumn;
        dials[i]->setBounds (5 + col * (dialSize + 4), top + row * (dialSize + 5), dialSize, dialSize);
    }
}

void DialColumn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff1c1a14));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (Theme::yellow.withAlpha (0.35f));
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
    if (header.isNotEmpty())
    {
        g.setColour (Theme::yellow);
        g.setFont (Theme::mono (12.0f));
        g.drawText (header, getLocalBounds().removeFromTop (18), juce::Justification::centred);
    }
}

void AddSlot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    juce::Path outline;
    outline.addRoundedRectangle (r, 9.0f);
    juce::Path dashed;
    const float dashes[] = { 7.0f, 5.0f };
    juce::PathStrokeType (1.4f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (isMouseOver() ? Theme::text.withAlpha (0.7f) : Theme::faint);
    g.fillPath (dashed);
    Icons::plus (g, r, isMouseOver() ? Theme::text : Theme::dim);
}

//==============================================================================
RackContent::RackContent (EditorContext& c) : ctx (c)
{
    addAndMakeVisible (addSlot);
    addSlot.onClick = [this] { if (onAddClicked) onAddClicked(); };
}

void RackContent::setMinWidth (int w)
{
    minWidth = w;
    setSize (juce::jmax (contentWidth, minWidth), panelH + pad * 2);
}

void RackContent::rebuild()
{
    panels.clear();
    columns.clear();

    auto& engine = ctx.getProcessor().engine;
    int x = pad;

    for (auto& m : engine.modules)
    {
        auto p = ModulePanel::create (ctx, *m);
        if (p == nullptr) continue;
        const int w = p->getPanelWidth();
        p->setBounds (x, pad, w, panelH);
        addAndMakeVisible (*p);
        panels.push_back (std::move (p));
        x += w + 6;

        for (int o = 0; o < m->getNumOutputs(); ++o)
        {
            if (engine.getOutgoing (m->uid, o).empty()) continue;
            auto col = std::make_unique<DialColumn> (ctx, *m, o, m->getNumOutputs() > 1 ? m->getOutputName (o) : juce::String());
            const int cw = col->getColumnWidth();
            col->setBounds (x, pad, cw, col->getPreferredHeight());
            addAndMakeVisible (*col);
            columns.push_back (std::move (col));
            x += cw + 6;
        }
        x += 6;
    }

    addSlot.setBounds (x, pad, 52, panelH);
    x += 52 + pad;
    contentWidth = x;
    setSize (juce::jmax (contentWidth, minWidth), panelH + pad * 2);
    repaint();
}

void RackContent::tick()
{
    for (auto& p : panels) p->tick();
    for (auto& c : columns) c->tick();
}

void RackContent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::rack);
    g.setColour (Theme::grid);
    for (int x = 0; x < getWidth(); x += 84) g.drawVerticalLine (x, 0.0f, (float) getHeight());
    for (int y = 0; y < getHeight(); y += 84) g.drawHorizontalLine (y, 0.0f, (float) getWidth());
}

//==============================================================================
int FooterButton::getIdealWidth() const
{
    return (int) Theme::textWidth (Theme::mono (12.0f), getName()) + 52;
}

void FooterButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (over || down)
    {
        g.setColour (down ? juce::Colour (0xff26262c) : juce::Colour (0xff1b1b20));
        g.fillRect (getLocalBounds());
    }
    auto r = getLocalBounds().reduced (12, 0);
    auto iconArea = r.removeFromLeft (18).toFloat();
    painter (g, iconArea, over ? juce::Colours::white : juce::Colour (0xff8f8ff0));
    r.removeFromLeft (8);
    g.setColour (over ? juce::Colours::white : Theme::text);
    g.setFont (Theme::mono (12.0f));
    g.drawText (getName(), r, juce::Justification::centredLeft);
}

//==============================================================================
class SettingsWindow::Content : public juce::Component
{
public:
    Content (BigModProcessor& p)
        : proc (p),
          pluginList (p.shared->formatManager, p.shared->knownList, p.shared->getDeadMansPedalFile(), p.shared->props.get(), false)
    {
        addAndMakeVisible (logo);

        const char* names[] = { "Plugins", "Audio", "License" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = tabs[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (77);
            b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff6b5b28));
            b.setColour (juce::TextButton::textColourOnId, Theme::yellow);
            b.onClick = [this, i] { show (i); };
            addAndMakeVisible (b);
        }

        // Plugins page
        installedLabel.setText ("Installed Plugins", juce::dontSendNotification);
        installedLabel.setFont (Theme::mono (13.0f));
        pluginList.setOptionsButtonText ("SCAN PLUGINS");
        sortToggle.setButtonText ("Sort By Manufacturer");
        sortToggle.setToggleState (p.shared->getSortByManufacturer(), juce::dontSendNotification);
        sortToggle.onClick = [this] { proc.shared->setSortByManufacturer (sortToggle.getToggleState()); };
        scanHint.setText ("SCAN PLUGINS > \"Scan for new or updated VST3 plug-ins\", choose your plug-in folders and press Scan. "
                          "BigMod only lists " + juce::String (BigModProcessor::isInstrument ? "instruments" : "effects")
                          + " in LOAD PLUGIN because this is the " + juce::String (BigModProcessor::isInstrument ? "Instrument" : "Effect") + " version.",
                          juce::dontSendNotification);
        scanHint.setFont (Theme::sans (12.5f));
        scanHint.setColour (juce::Label::textColourId, Theme::dim);
        for (juce::Component* c : { (juce::Component*) &installedLabel, (juce::Component*) &pluginList,
                                    (juce::Component*) &sortToggle, (juce::Component*) &scanHint })
            pluginsPage.addAndMakeVisible (c);
        addChildComponent (pluginsPage);

        // Audio page
        resLabel.setText ("Modulation resolution", juce::dontSendNotification);
        resLabel.setFont (Theme::mono (13.0f));
        const int sizes[] = { 8, 16, 32, 64, 128, 256 };
        for (int s : sizes) resBox.addItem (juce::String (s) + " samples", s);
        resBox.setSelectedId (proc.resolution.load(), juce::dontSendNotification);
        resBox.onChange = [this]
        {
            proc.resolution.store (resBox.getSelectedId());
            proc.shared->setDefaultResolution (resBox.getSelectedId());
        };
        resHint.setText ("How often modulators update the plug-in. Smaller = smoother, faster movement but more CPU. 32 is a good default.",
                         juce::dontSendNotification);
        resHint.setFont (Theme::sans (12.5f));
        resHint.setColour (juce::Label::textColourId, Theme::dim);
        info.setMultiLine (true);
        info.setReadOnly (true);
        info.setFont (Theme::mono (12.5f, false));
        info.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f0f12));
        for (juce::Component* c : { (juce::Component*) &resLabel, (juce::Component*) &resBox,
                                    (juce::Component*) &resHint, (juce::Component*) &info })
            audioPage.addAndMakeVisible (c);
        addChildComponent (audioPage);

        // License page
        license.setMultiLine (true);
        license.setReadOnly (true);
        license.setFont (Theme::sans (13.5f));
        license.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f0f12));
        license.setText ("BigMod 1.0\n\n"
                         "A modulation rack that snaps onto any VST3 / AU plug-in: macros, LFOs with a curve editor, XY pads, "
                         "a preset blender, envelope followers, randomizers, MIDI macros and cross-modulation.\n\n"
                         "Free to use. No licence key is needed.\n\n"
                         "Built with the JUCE framework (juce.com) and the Steinberg VST3 SDK. VST is a trademark of "
                         "Steinberg Media Technologies GmbH. If you distribute BigMod, follow the JUCE licence terms "
                         "(AGPLv3 for open-source distribution, or a JUCE commercial licence).\n\n"
                         "Settings and the plug-in list are stored in:\n" + proc.shared->props->getFile().getFullPathName()
                         + "\n\nPresets are stored in:\n" + proc.shared->getPresetFolder().getFullPathName());
        addChildComponent (license);

        setSize (700, 560);
        tabs[0].setToggleState (true, juce::dontSendNotification);
        show (0);
    }

    void show (int i)
    {
        tabs[(size_t) i].setToggleState (true, juce::dontSendNotification);
        pluginsPage.setVisible (i == 0);
        audioPage.setVisible (i == 1);
        license.setVisible (i == 2);
        if (i == 1) refreshInfo();
    }

    void refreshInfo()
    {
        juce::String s;
        s << "Mode:           " << (BigModProcessor::isInstrument ? "Instrument" : "Effect") << "\n"
          << "Sample rate:    " << juce::String (proc.getSampleRate(), 0) << " Hz\n"
          << "Host block:     " << proc.getBlockSize() << " samples\n"
          << "Channels:       " << proc.getTotalNumInputChannels() << " in / " << proc.getTotalNumOutputChannels() << " out\n"
          << "Latency:        " << proc.getLatencySamples() << " samples\n"
          << "Loaded plug-in: " << (proc.getHostedPlugin() != nullptr ? proc.getHostedName() : juce::String ("none")) << "\n";
        if (proc.isStandalone())
            s << "\nStandalone: choose your audio and MIDI devices with the \"Options\" button at the top of the window.\n";
        info.setText (s);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0d10)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 16);
        logo.setBounds (r.removeFromTop (44));
        r.removeFromTop (8);
        auto tabRow = r.removeFromTop (26).withSizeKeepingCentre (3 * 86 + 16, 26);
        for (auto& t : tabs) { t.setBounds (tabRow.removeFromLeft (86)); tabRow.removeFromLeft (8); }
        r.removeFromTop (16);

        pluginsPage.setBounds (r);
        audioPage.setBounds (r);
        license.setBounds (r);

        auto p = pluginsPage.getLocalBounds();
        installedLabel.setBounds (p.removeFromTop (22));
        auto bottom = p.removeFromBottom (54);
        pluginList.setBounds (p);
        auto row = bottom.removeFromTop (30).withTrimmedTop (4);
        sortToggle.setBounds (row.removeFromRight (200));
        scanHint.setBounds (bottom);

        auto a = audioPage.getLocalBounds();
        auto rrow = a.removeFromTop (28);
        resLabel.setBounds (rrow.removeFromLeft (200));
        resBox.setBounds (rrow.removeFromLeft (140));
        resHint.setBounds (a.removeFromTop (44));
        a.removeFromTop (8);
        info.setBounds (a);
    }

private:
    BigModProcessor& proc;
    Logo logo { 26.0f };
    std::array<juce::TextButton, 3> tabs;
    juce::Component pluginsPage, audioPage;
    juce::Label installedLabel, scanHint, resLabel, resHint;
    juce::PluginListComponent pluginList;
    juce::ToggleButton sortToggle;
    juce::ComboBox resBox;
    juce::TextEditor info, license;
};

SettingsWindow::SettingsWindow (BigModProcessor& p, juce::LookAndFeel& lf)
    : juce::DocumentWindow ("BigMod Settings", Theme::bg, juce::DocumentWindow::closeButton)
{
    setLookAndFeel (&lf);
    setUsingNativeTitleBar (true);
    content = new Content (p);
    setContentOwned (content, true);
    setResizable (true, false);
    setResizeLimits (560, 420, 1400, 1200);
    setAlwaysOnTop (true);
    centreWithSize (getWidth(), getHeight());
}

SettingsWindow::~SettingsWindow()
{
    clearContentComponent();
    setLookAndFeel (nullptr);
}

void SettingsWindow::showTab (int i) { if (content != nullptr) content->show (i); }

//==============================================================================
BigModEditor::BigModEditor (BigModProcessor& p)
    : AudioProcessorEditor (p), proc (p), rack (*this)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (false, true, false, true);
    rackViewport.setScrollBarThickness (8);
    addAndMakeVisible (rackViewport);
    rack.onAddClicked = [this] { showAddModuleMenu(); };

    addAndMakeVisible (logo);
    gearBtn.setTooltip ("Settings: scan plug-ins, audio, licence");
    gearBtn.colour = juce::Colour (0xff8f8ff0);
    gearBtn.onClick = [this] { openSettings (0); };
    addAndMakeVisible (gearBtn);
    loadBtn.setTooltip ("Load a plug-in into BigMod");
    loadBtn.onClick = [this] { showLoadMenu(); };
    addAndMakeVisible (loadBtn);
    presetsBtn.setTooltip ("Save and load BigMod presets");
    presetsBtn.onClick = [this] { showPresetsMenu(); };
    addAndMakeVisible (presetsBtn);
    collapseBtn.setTooltip ("Show / hide the modulation rack");
    collapseBtn.colour = juce::Colour (0xff8f8ff0);
    collapseBtn.onClick = [this] { collapsed = ! collapsed; updateSize(); };
    addAndMakeVisible (collapseBtn);

    addChildComponent (bubble);

    proc.onBeforePluginChange = [this] { releaseHostedEditor(); };
    proc.onAfterPluginChange  = [this] { rebuildHostedEditor(); rebuildRack(); };

    rebuildHostedEditor();
    rebuildRack();
    if (getWidth() == 0) updateSize();
    startTimerHz (30);
}

BigModEditor::~BigModEditor()
{
    stopTimer();
    proc.onBeforePluginChange = nullptr;
    proc.onAfterPluginChange = nullptr;
    proc.engine.learnSourceUid.store (0);
    settings.reset();
    releaseHostedEditor();
    setLookAndFeel (nullptr);
}

//==============================================================================
void BigModEditor::releaseHostedEditor()
{
    if (hostedEditor == nullptr) return;
    hostedEditor->removeComponentListener (this);
    removeChildComponent (hostedEditor.get());
    hostedEditor.reset();
}

void BigModEditor::rebuildHostedEditor()
{
    releaseHostedEditor();

    if (auto* plug = proc.getHostedPlugin())
    {
        juce::AudioProcessorEditor* ed = plug->hasEditor() ? plug->createEditorIfNeeded() : nullptr;
        if (ed == nullptr)
        {
            ed = new juce::GenericAudioProcessorEditor (*plug);
            ed->setSize (juce::jlimit (420, 760, ed->getWidth()), juce::jlimit (200, 520, ed->getHeight()));
        }
        hostedEditor.reset (ed);
        addAndMakeVisible (*ed);
        ed->addComponentListener (this);
    }
    updateSize();
}

void BigModEditor::updateSize()
{
    const int hw = hostedEditor != nullptr ? hostedEditor->getWidth() : 0;
    const int hh = hostedEditor != nullptr ? hostedEditor->getHeight() : 0;
    const bool showRack = ! collapsed || hostedEditor == nullptr;
    const int w = juce::jmax (860, hw);
    const int h = hh + (showRack ? rackH : 0) + footerH;

    if (getWidth() != w || getHeight() != h)
        setSize (w, h);
    else
        resized();
}

void BigModEditor::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (wasResized && &c == hostedEditor.get())
        updateSize();
}

void BigModEditor::resized()
{
    auto r = getLocalBounds();
    auto f = r.removeFromBottom (footerH);

    if (hostedEditor != nullptr)
    {
        hostedEditor->setTopLeftPosition ((getWidth() - hostedEditor->getWidth()) / 2, 0);
        r.removeFromTop (hostedEditor->getHeight());
    }

    const bool showRack = ! collapsed || hostedEditor == nullptr;
    rackViewport.setVisible (showRack);
    rackViewport.setBounds (r);
    rack.setMinWidth (r.getWidth());

    // footer
    logo.setBounds (f.removeFromLeft (150));
    gearBtn.setBounds (f.removeFromLeft (48));
    loadBtn.setBounds (f.removeFromLeft (loadBtn.getIdealWidth()));
    presetsBtn.setBounds (f.removeFromLeft (presetsBtn.getIdealWidth()));
    collapseBtn.setBounds (f.removeFromRight (44));
    collapseBtn.setVisible (hostedEditor != nullptr);
    statusArea = f.reduced (14, 0);
}

void BigModEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    auto f = getLocalBounds().removeFromBottom (footerH);
    juce::ColourGradient grad (juce::Colour (0xff1b1b20), 0.0f, (float) f.getY(), Theme::footer, 0.0f, (float) f.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (f);
    g.setColour (juce::Colour (0xff2a2a31));
    g.drawHorizontalLine (f.getY(), 0.0f, (float) getWidth());

    for (auto* c : { (juce::Component*) &logo, (juce::Component*) &gearBtn, (juce::Component*) &loadBtn, (juce::Component*) &presetsBtn })
        g.drawVerticalLine (c->getRight(), (float) f.getY() + 1.0f, (float) f.getBottom());

    if (! statusArea.isEmpty())
    {
        g.setFont (Theme::mono (11.5f, false));
        g.setColour (proc.lastError.isNotEmpty() ? Theme::red : isLearning() ? Theme::yellow : Theme::dim);
        g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 1, 0.8f);
    }
}

bool BigModEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && isLearning())
    {
        cancelLearn();
        return true;
    }
    return false;
}

//==============================================================================
void BigModEditor::rebuildRack()
{
    bubble.setVisible (false);
    bubble.anchor = nullptr;
    needsRebuild = false;
    lastVersion = proc.engine.structureVersion.load();
    rack.rebuild();
    rack.setMinWidth (rackViewport.getWidth());
}

void BigModEditor::deferredRemoveModule (int uid)
{
    juce::Component::SafePointer<BigModEditor> safe (this);
    juce::MessageManager::callAsync ([safe, uid]
    {
        if (safe == nullptr) return;
        safe->proc.engine.removeModule (uid);
        safe->rebuildRack();
    });
}

void BigModEditor::timerCallback()
{
    flashPhase += 0.21f;
    flash = 0.5f + 0.5f * std::sin (flashPhase);

    auto& engine = proc.engine;

    if (engine.isLearning())
    {
        const int hit = engine.learnHostHit.exchange (-1);
        if (hit >= 0)
        {
            engine.addHostAssignment (engine.learnSourceUid.load(), engine.learnSourceOutput.load(), hit, 0.25f);
            engine.learnSourceUid.store (0);
            needsRebuild = true;
        }
    }

    if (needsRebuild || engine.structureVersion.load() != lastVersion)
    {
        // never rebuild while the user is dragging something inside the rack
        if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            rebuildRack();
    }

    rack.tick();

    juce::String s;
    if (proc.lastError.isNotEmpty())
        s = proc.lastError;
    else if (engine.isLearning())
    {
        auto* m = engine.findModule (engine.learnSourceUid.load());
        s = "ASSIGN " + (m != nullptr ? m->getName() : juce::String()) + ": move a control in the plug-in, or click a flashing control.  (Esc cancels)";
    }
    else if (auto* plug = proc.getHostedPlugin())
        s = plug->getName() + "  -  " + juce::String (engine.getNumHostParams()) + " parameters";
    else
        s = juce::String ("No plug-in loaded. Click LOAD PLUGIN") + (proc.shared->knownList.getNumTypes() == 0 ? " (scan first: gear icon)." : ".");

    if (s != status)
    {
        status = s;
        repaint (statusArea);
    }
    else if (engine.isLearning())
        repaint (statusArea);
}

//==============================================================================
bool BigModEditor::isLearning() const { return proc.engine.isLearning(); }

bool BigModEditor::isLearningFrom (int uid, int out) const
{
    return proc.engine.learnSourceUid.load() == uid && proc.engine.learnSourceOutput.load() == out;
}

void BigModEditor::toggleLearn (Module& m, int out)
{
    if (isLearningFrom (m.uid, out))
    {
        cancelLearn();
        return;
    }
    proc.engine.learnHostHit.store (-1);
    proc.engine.learnSourceOutput.store (out);
    proc.engine.learnSourceUid.store (m.uid);
    grabKeyboardFocus();
    rack.repaint();
}

void BigModEditor::cancelLearn()
{
    if (! isLearning()) return;
    proc.engine.learnSourceUid.store (0);
    rack.repaint();
}

bool BigModEditor::canBeLearnTarget (const Module& m, const ModParam& p) const
{
    return isLearning() && p.modulatable && m.uid != proc.engine.learnSourceUid.load();
}

bool BigModEditor::learnClick (Module& target, ModParam& p)
{
    if (! isLearning()) return false;

    if (canBeLearnTarget (target, p))
        proc.engine.addModuleAssignment (proc.engine.learnSourceUid.load(), proc.engine.learnSourceOutput.load(),
                                         target.uid, p.id, 0.5f);
    proc.engine.learnSourceUid.store (0);
    needsRebuild = true;     // rebuilt on the next timer tick, not inside this click
    rack.repaint();
    return true;
}

void BigModEditor::showHostParamMenu (Module& m, int out)
{
    auto& engine = proc.engine;
    const int n = engine.getNumHostParams();
    juce::PopupMenu menu;
    if (n == 0)
    {
        menu.addItem (-1, "Load a plug-in first", false);
    }
    else
    {
        menu.addSectionHeader ("Modulate a " + proc.getHostedName() + " parameter");
        const int chunk = 64;
        if (n <= chunk)
            for (int i = 0; i < n; ++i) menu.addItem (i + 1, proc.getHostedParamName (i), true, engine.isHostParamManaged (i));
        else
            for (int s = 0; s < n; s += chunk)
            {
                juce::PopupMenu sub;
                for (int i = s; i < juce::jmin (n, s + chunk); ++i)
                    sub.addItem (i + 1, proc.getHostedParamName (i), true, engine.isHostParamManaged (i));
                menu.addSubMenu (juce::String (s + 1) + " - " + juce::String (juce::jmin (n, s + chunk)), sub);
            }
    }

    juce::Component::SafePointer<BigModEditor> safe (this);
    const int uid = m.uid;
    menu.showMenuAsync (juce::PopupMenu::Options(), [safe, uid, out] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        safe->proc.engine.addHostAssignment (uid, out, r - 1, 0.25f);
        safe->proc.engine.learnSourceUid.store (0);
        safe->rebuildRack();
    });
}

void BigModEditor::showBubble (juce::Component& anchor, const juce::String& t, const juce::String& s, const juce::String& v)
{
    bubble.anchor = &anchor;
    bubble.set (t, s, v);
    auto a = getLocalArea (&anchor, anchor.getLocalBounds());
    const int w = juce::jmax (120, (int) Theme::textWidth (Theme::mono (11.0f), juce::jmax (t.length(), s.length()) > 0 ? (t.length() > s.length() ? t : s) : t) + 20);
    juce::Rectangle<int> b (a.getRight() + 6, a.getY() - 8, juce::jmin (w, 260), 58);
    if (b.getRight() > getWidth()) b.setX (a.getX() - b.getWidth() - 6);
    b.setY (juce::jlimit (0, juce::jmax (0, getHeight() - b.getHeight()), b.getY()));
    bubble.setBounds (b);
    bubble.setVisible (true);
    bubble.toFront (false);
}

void BigModEditor::hideBubble (juce::Component& anchor)
{
    if (bubble.anchor == &anchor)
    {
        bubble.setVisible (false);
        bubble.anchor = nullptr;
    }
}

//==============================================================================
void BigModEditor::openSettings (int tab)
{
    if (settings == nullptr)
        settings = std::make_unique<SettingsWindow> (proc, lnf);
    settings->showTab (tab);
    settings->setVisible (true);
    settings->toFront (true);
}

void BigModEditor::showLoadMenu()
{
    auto& sh = *proc.shared;
    sh.reloadListIfChangedOnDisk();

    juce::Array<juce::PluginDescription> types;
    for (auto& t : sh.knownList.getTypes())
        if (t.isInstrument == BigModProcessor::isInstrument && ! t.name.containsIgnoreCase ("BigMod"))
            types.add (t);

    juce::PopupMenu m;
    if (types.isEmpty())
    {
        m.addItem (-1, BigModProcessor::isInstrument ? "No instrument plug-ins found" : "No effect plug-ins found", false);
        m.addItem (1, "Scan plug-ins...");
    }
    else
    {
        juce::String ticked;
        if (auto* h = proc.getHostedPlugin()) ticked = h->getPluginDescription().createIdentifierString();
        juce::KnownPluginList::addToMenu (m, types, sh.getSortByManufacturer() ? juce::KnownPluginList::sortByManufacturer
                                                                                : juce::KnownPluginList::sortAlphabetically, ticked);
        m.addSeparator();
        m.addItem (1, "Scan plug-ins...");
    }
    m.addItem (2, "Unload plug-in", proc.getHostedPlugin() != nullptr);

    juce::Component::SafePointer<BigModEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBtn), [safe, types] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) { safe->openSettings (0); return; }
        if (r == 2) { safe->proc.unloadPlugin(); return; }

        const int idx = juce::KnownPluginList::getIndexChosenByMenu (types, r);
        if (! juce::isPositiveAndBelow (idx, types.size())) return;

        juce::String err;
        safe->status = "Loading " + types[idx].name + "...";
        safe->repaint();
        if (! safe->proc.loadPlugin (types[idx], err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "BigMod",
                                                    "Couldn't load " + types[idx].name + ":\n" + err);
    });
}

void BigModEditor::showPresetsMenu()
{
    auto folder = proc.shared->getPresetFolder();
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.bigmod");
    files.sort();

    juce::PopupMenu m;
    m.addItem (1, "Save preset...");
    m.addItem (2, "Load preset from file...");
    m.addItem (4, "Open presets folder");
    m.addSeparator();
    m.addItem (3, "Clear rack (remove all modules)", ! proc.engine.modules.empty());
    if (! files.isEmpty())
    {
        m.addSectionHeader ("Presets");
        for (int i = 0; i < files.size(); ++i)
            m.addItem (100 + i, files[i].getFileNameWithoutExtension());
    }

    juce::Component::SafePointer<BigModEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsBtn), [safe, files, folder] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& self = *safe;

        if (r == 1)
        {
            self.chooser = std::make_unique<juce::FileChooser> ("Save BigMod preset", folder.getChildFile ("My Preset.bigmod"), "*.bigmod");
            self.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                                       [safe] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe == nullptr || f == juce::File()) return;
                if (! safe->proc.savePresetToFile (f.withFileExtension ("bigmod")))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "BigMod", "Couldn't save the preset.");
            });
        }
        else if (r == 2)
        {
            self.chooser = std::make_unique<juce::FileChooser> ("Load BigMod preset", folder, "*.bigmod");
            self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [safe] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe != nullptr && f.existsAsFile() && ! safe->proc.loadPresetFromFile (f))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "BigMod", "That isn't a BigMod preset.");
            });
        }
        else if (r == 3)
        {
            self.proc.clearRack();
            self.rebuildRack();
        }
        else if (r == 4)
        {
            folder.startAsProcess();
        }
        else if (r >= 100 && r - 100 < files.size())
        {
            self.proc.loadPresetFromFile (files[r - 100]);
        }
    });
}

void BigModEditor::showAddModuleMenu()
{
    juce::PopupMenu m;
    for (int t = 0; t < (int) ModType::NumTypes; ++t)
    {
        juce::String why;
        const bool ok = proc.engine.canAddModule ((ModType) t, why);
        m.addItem (t + 1, modTypeMenuName ((ModType) t) + (ok ? juce::String() : "  (" + why + ")"), ok);
    }

    juce::Component::SafePointer<BigModEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        safe->proc.engine.addModule ((ModType) (r - 1));
        safe->rebuildRack();
        auto& vp = safe->rackViewport;
        vp.setViewPosition (juce::jmax (0, safe->rack.getWidth() - vp.getWidth()), 0);
    });
}
