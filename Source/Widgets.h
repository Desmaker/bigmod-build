#pragma once
#include "Theme.h"
#include "PluginProcessor.h"

//==============================================================================
// What module panels need from the editor
//==============================================================================
class EditorContext
{
public:
    virtual ~EditorContext() = default;
    virtual BigModProcessor& getProcessor() = 0;

    // Assign / learn
    virtual bool isLearning() const = 0;
    virtual bool isLearningFrom (int moduleUid, int output) const = 0;
    virtual void toggleLearn (Module&, int output) = 0;
    virtual void cancelLearn() = 0;
    virtual bool canBeLearnTarget (const Module&, const ModParam&) const = 0;
    virtual bool learnClick (Module& target, ModParam&) = 0;           // true if the click was consumed
    virtual void showHostParamMenu (Module&, int output) = 0;

    // Hover info bubble
    virtual void showBubble (juce::Component& anchor, const juce::String& title,
                             const juce::String& sub, const juce::String& value) = 0;
    virtual void hideBubble (juce::Component& anchor) = 0;

    virtual float getFlash() const = 0;                                  // 0..1 pulse for blinking
    virtual void structureChanged() = 0;                                 // rebuild the rack soon
    virtual void deferredRemoveModule (int uid) = 0;                     // safe to call from a panel's own button
};

//==============================================================================
// Base for every control bound to a module parameter. Handles dragging, typing,
// the learn-mode highlight and "click to receive modulation".
//==============================================================================
class ParamControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ParamControl (EditorContext&, Module&, ModParam&);
    ~ParamControl() override;

    Module& module;
    ModParam& param;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void paintOverChildren (juce::Graphics&) override;

    bool isDragging() const { return dragging; }
    juce::String getDisplayText() const;

protected:
    EditorContext& ctx;
    float sensitivity = 0.005f;
    virtual juce::Rectangle<int> getEditorBounds() { return getLocalBounds(); }

private:
    float startNorm = 0.0f;
    bool dragging = false;
    std::unique_ptr<juce::TextEditor> textEditor;
    void startTextEdit();
};

//==============================================================================
class ValueBox : public ParamControl
{
public:
    enum class Style { Labelled, Boxed };
    ValueBox (EditorContext&, Module&, ModParam&, Style, juce::String label = {});
    void paint (juce::Graphics&) override;

private:
    Style style;
    juce::String label;
    juce::Rectangle<int> getEditorBounds() override;
};

//==============================================================================
class ModKnob : public ParamControl
{
public:
    ModKnob (EditorContext&, Module&, ModParam&, bool bipolar, juce::Colour arcColour);
    void paint (juce::Graphics&) override;

private:
    bool bipolar;
    juce::Colour arcColour;
};

//==============================================================================
// The little modulation-amount dials. Yellow = outgoing (sits next to the
// modulator), orange = incoming (sits under the parameter being modulated).
//==============================================================================
class DepthDial : public juce::Component
{
public:
    DepthDial (EditorContext&, int assignmentUid, bool incoming);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    const int assignmentUid;
    const bool incoming;

private:
    EditorContext& ctx;
    float startDepth = 0.0f;
    Assignment* get() const;
    void updateBubble();
    void setDepth (float);
    juce::String label;
};

//==============================================================================
class HoverBubble : public juce::Component
{
public:
    HoverBubble() { setInterceptsMouseClicks (false, false); setAlwaysOnTop (true); }
    void set (juce::String t, juce::String s, juce::String v) { title = t; sub = s; value = v; repaint(); }
    void paint (juce::Graphics&) override;
    juce::Component* anchor = nullptr;
private:
    juce::String title, sub, value;
};

//==============================================================================
class PathButton : public juce::Button
{
public:
    using Painter = std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;
    PathButton (const juce::String& name, Painter p) : juce::Button (name), painter (std::move (p)) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::Colour colour = Theme::dim, overColour = Theme::text;
    bool drawBackground = false;
private:
    Painter painter;
};

namespace Icons
{
    void gear (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void plus (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void list (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void cross (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void collapse (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void download (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
    void chevron (juce::Graphics&, juce::Rectangle<float>, juce::Colour, bool up);
    void playMode (juce::Graphics&, juce::Rectangle<float>, juce::Colour, int mode);
}

//==============================================================================
class PowerLed : public juce::Button
{
public:
    PowerLed() : juce::Button ("power") { setClickingTogglesState (true); setTooltip ("Enable / bypass this module"); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class AssignButton : public juce::Button
{
public:
    AssignButton (EditorContext& c, const juce::String& text) : juce::Button (text), ctx (c) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool learning = false;
    juce::Colour colour = Theme::yellow;
private:
    EditorContext& ctx;
};

class SlotCombo : public juce::ComboBox
{
public:
    std::function<void()> onBeforePopup;
    void showPopup() override { if (onBeforePopup) onBeforePopup(); juce::ComboBox::showPopup(); }
};
