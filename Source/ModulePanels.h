#pragma once
#include "Widgets.h"

namespace RackMetrics
{
    constexpr int panelH = 172;      // module panel height
    constexpr int pad = 12;          // around the rack contents
    constexpr int rackH = panelH + pad * 2 + 10;   // + scrollbar
    constexpr int footerH = 40;
    constexpr int headerH = 28;
}

//==============================================================================
class ModulePanel : public juce::Component
{
public:
    ModulePanel (EditorContext&, Module&);
    ~ModulePanel() override = default;

    static std::unique_ptr<ModulePanel> create (EditorContext&, Module&);

    Module& module;
    virtual int getPanelWidth() const = 0;
    virtual void tick();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

protected:
    virtual void layoutBody (juce::Rectangle<int>) = 0;
    virtual bool nameInHeader() const { return true; }
    virtual void paintBody (juce::Graphics&) {}

    ValueBox* addValueBox (const juce::String& paramId, ValueBox::Style, const juce::String& label = {});
    ModKnob*  addKnob (const juce::String& paramId, bool bipolar, juce::Colour);
    void addAssignButton (const juce::String& text, int output);

    EditorContext& ctx;
    PowerLed power;
    PathButton closeBtn { "close", Icons::cross };
    std::vector<std::unique_ptr<AssignButton>> assigns;
    std::vector<int> assignOutputs;
    std::vector<std::unique_ptr<ParamControl>> controls;
    std::vector<std::unique_ptr<DepthDial>> incomingDials;
    juce::Rectangle<int> nameArea;
    bool builtIncoming = false;
    bool suppressAssignClick = false;

private:
    void layoutIncomingDials();
};

//==============================================================================
class HistoryView : public juce::Component
{
public:
    HistoryView (juce::Colour c, bool bip) : colour (c), bipolar (bip) { setInterceptsMouseClicks (false, false); }
    void push (float v) { buf[(size_t) pos] = v; pos = (pos + 1) % (int) buf.size(); repaint(); }
    void paint (juce::Graphics&) override;
private:
    juce::Colour colour;
    bool bipolar;
    std::array<float, 140> buf {};
    int pos = 0;
};

//==============================================================================
class LfoCurveView : public juce::Component, public juce::SettableTooltipClient
{
public:
    LfoCurveView (EditorContext&, LfoModule&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    std::function<void()> onEdited;

private:
    EditorContext& ctx;
    LfoModule& lfo;
    int dragNode = -1, pairedNode = -1, dragSeg = -1, hoverNode = -1, hoverSeg = -1;
    float segStartBend = 0.0f, segPosAtDown = 0.5f, pairedStartY = 0.0f, nodeStartY = 0.0f;
    bool segPosSet = false;

    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (5.0f, 6.0f); }
    float toX (float x) const  { auto a = area(); return a.getX() + x * a.getWidth(); }
    float toY (float y) const  { auto a = area(); return a.getCentreY() - y * a.getHeight() * 0.5f; }
    float fromX (float px) const { auto a = area(); return juce::jlimit (0.0f, 1.0f, (px - a.getX()) / a.getWidth()); }
    float fromY (float py) const { auto a = area(); return juce::jlimit (-1.0f, 1.0f, (a.getCentreY() - py) / (a.getHeight() * 0.5f)); }
    int hitNode (juce::Point<float>) const;
    int hitSegment (juce::Point<float>) const;
    void apply (const Curve&);
    void editNodeDialog (int);
    void editSegmentDialog (int);
};

//==============================================================================
class XYPadView : public juce::Component, public juce::SettableTooltipClient
{
public:
    XYPadView (EditorContext&, XYModule&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    EditorContext& ctx;
    XYModule& xy;
    bool dragging = false;
    void setFrom (juce::Point<float>);
};

class BlendGraphView : public juce::Component, public juce::SettableTooltipClient
{
public:
    BlendGraphView (EditorContext&, BlenderModule&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    EditorContext& ctx;
    BlenderModule& bl;
    bool dragging = false;
    float ringRadius() const { return (float) juce::jmin (getWidth(), getHeight()) * 0.5f - 15.0f; }
    void setFrom (juce::Point<float>);
};

//==============================================================================
class MacroPanel : public ModulePanel
{
public:
    MacroPanel (EditorContext&, Module&);
    int getPanelWidth() const override { return 118; }
    void tick() override;
protected:
    void layoutBody (juce::Rectangle<int>) override;
    bool nameInHeader() const override { return false; }
    void paintBody (juce::Graphics&) override;
private:
    ModKnob* knob = nullptr;
};

class LfoPanel : public ModulePanel
{
public:
    LfoPanel (EditorContext&, Module&);
    int getPanelWidth() const override { return 330; }
    void tick() override;
protected:
    void layoutBody (juce::Rectangle<int>) override;
private:
    LfoModule& lfo;
    LfoCurveView view;
    juce::ComboBox shape, syncBox;
    PathButton up, down, modeBtn;
    ValueBox* phase = nullptr;
    ValueBox* rate = nullptr;
    void selectPreset (int);
    void refreshShapeBox();
};

class XYPanel : public ModulePanel
{
public:
    XYPanel (EditorContext&, Module&);
    int getPanelWidth() const override { return 184; }
    void tick() override;
protected:
    void layoutBody (juce::Rectangle<int>) override;
    bool nameInHeader() const override { return false; }
    void paintBody (juce::Graphics&) override;
private:
    XYPadView pad;
    juce::Rectangle<int> xLabel, yLabel;
};

class BlenderPanel : public ModulePanel
{
public:
    BlenderPanel (EditorContext&, Module&);
    int getPanelWidth() const override { return 356; }
    void tick() override;
protected:
    void layoutBody (juce::Rectangle<int>) override;
    void paintBody (juce::Graphics&) override;
private:
    BlenderModule& bl;
    BlendGraphView graph;
    std::array<SlotCombo, 4> slotBoxes;
    std::array<std::unique_ptr<PathButton>, 4> captureBtns;
    std::array<juce::Rectangle<int>, 4> numberAreas;
    juce::Rectangle<int> rangeLabel, angleLabel;
    int lastSlotsVersion = -1;
    void refreshSlots();
    void fillSlotMenu (int slot);
};

class EnvPanel : public ModulePanel
{
public:
    EnvPanel (EditorContext&, Module&);
    int getPanelWidth() const override { return 316; }
    void tick() override;
protected:
    void layoutBody (juce::Rectangle<int>) override;
private:
    EnvFollowerModule& env;
    HistoryView hist { Theme::env, false };
    juce::ComboBox modeBox;
    juce::TextButton srcBtn;
};

class RandomPanel : public ModulePanel
{
public:
    RandomPanel (EditorContext&, Module&);
    int getPanelWidth() const override { return 324; }
    void tick() override;
protected:
    void layoutBody (juce::Rectangle<int>) override;
private:
    RandomModule& rnd;
    HistoryView hist { Theme::random, true };
    juce::ComboBox syncBox;
};

class MidiMacroPanel : public ModulePanel
{
public:
    MidiMacroPanel (EditorContext&, Module&);
    int getPanelWidth() const override { return 118; }
    void tick() override;
protected:
    void layoutBody (juce::Rectangle<int>) override;
    bool nameInHeader() const override { return false; }
    void paintBody (juce::Graphics&) override;
private:
    MidiMacroModule& midi;
    ModKnob* knob = nullptr;
    AssignButton learnBtn;
};
