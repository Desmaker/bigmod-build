#pragma once
#include "ModulePanels.h"

//==============================================================================
class Logo : public juce::Component
{
public:
    explicit Logo (float textHeight = 21.0f) : h (textHeight) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
private:
    float h;
};

//==============================================================================
class DialColumn : public juce::Component
{
public:
    DialColumn (EditorContext&, Module&, int output, juce::String header);
    int getColumnWidth() const;
    int getPreferredHeight() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() { for (auto& d : dials) d->repaint(); }
private:
    std::vector<std::unique_ptr<DepthDial>> dials;
    juce::String header;
    static constexpr int dialSize = 30, perColumn = 4;
};

class AddSlot : public juce::Component, public juce::SettableTooltipClient
{
public:
    AddSlot() { setRepaintsOnMouseActivity (true); setTooltip ("Add a modulator"); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && contains (e.getPosition())) onClick(); }
    std::function<void()> onClick;
};

//==============================================================================
class RackContent : public juce::Component
{
public:
    explicit RackContent (EditorContext&);
    void rebuild();
    void tick();
    void setMinWidth (int w);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { ctx.cancelLearn(); }

    std::function<void()> onAddClicked;
    int contentWidth = 0;

private:
    EditorContext& ctx;
    std::vector<std::unique_ptr<ModulePanel>> panels;
    std::vector<std::unique_ptr<DialColumn>> columns;
    AddSlot addSlot;
    int minWidth = 0;
};

//==============================================================================
class FooterButton : public juce::Button
{
public:
    FooterButton (const juce::String& text, PathButton::Painter icon) : juce::Button (text), painter (std::move (icon)) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    int getIdealWidth() const;
private:
    PathButton::Painter painter;
};

//==============================================================================
class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow (BigModProcessor&, juce::LookAndFeel&);
    ~SettingsWindow() override;
    void closeButtonPressed() override { setVisible (false); }
    void showTab (int);
private:
    class Content;
    Content* content = nullptr;
};

//==============================================================================
class BigModEditor : public juce::AudioProcessorEditor,
                     public EditorContext,
                     private juce::Timer,
                     private juce::ComponentListener
{
public:
    explicit BigModEditor (BigModProcessor&);
    ~BigModEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // EditorContext
    BigModProcessor& getProcessor() override { return proc; }
    bool isLearning() const override;
    bool isLearningFrom (int uid, int output) const override;
    void toggleLearn (Module&, int output) override;
    void cancelLearn() override;
    bool canBeLearnTarget (const Module&, const ModParam&) const override;
    bool learnClick (Module&, ModParam&) override;
    void showHostParamMenu (Module&, int output) override;
    void showBubble (juce::Component&, const juce::String&, const juce::String&, const juce::String&) override;
    void hideBubble (juce::Component&) override;
    float getFlash() const override { return flash; }
    void structureChanged() override { needsRebuild = true; }
    void deferredRemoveModule (int uid) override;

    // used by the test harness
    void rebuildRackNow() { rebuildRack(); }

private:
    BigModProcessor& proc;
    BigModLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<juce::AudioProcessorEditor> hostedEditor;
    juce::Viewport rackViewport;
    RackContent rack;

    Logo logo;
    PathButton gearBtn { "settings", Icons::gear };
    FooterButton loadBtn { "LOAD PLUGIN", Icons::plus };
    FooterButton presetsBtn { "PRESETS", Icons::list };
    PathButton collapseBtn { "collapse", Icons::collapse };
    juce::String status;
    juce::Rectangle<int> statusArea;

    HoverBubble bubble;
    std::unique_ptr<SettingsWindow> settings;
    std::unique_ptr<juce::FileChooser> chooser;

    bool collapsed = false, needsRebuild = false;
    int lastVersion = -1;
    float flash = 0.0f, flashPhase = 0.0f;

    void rebuildHostedEditor();
    void releaseHostedEditor();
    void updateSize();
    void rebuildRack();
    void showLoadMenu();
    void showPresetsMenu();
    void showAddModuleMenu();
    void openSettings (int tab = 0);
    void layoutFooter();
    void paintFooter (juce::Graphics&);

    void timerCallback() override;
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BigModEditor)
};
