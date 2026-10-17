#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
namespace Theme
{
    const juce::Colour bg          { 0xff111114 };
    const juce::Colour rack        { 0xff16161a };
    const juce::Colour grid        { 0xff1d1d22 };
    const juce::Colour panel       { 0xff222227 };
    const juce::Colour panelTop    { 0xff2a2a30 };
    const juce::Colour border      { 0xff35353d };
    const juce::Colour footer      { 0xff0c0c0e };
    const juce::Colour text        { 0xffdcdce2 };
    const juce::Colour dim         { 0xff8a8a96 };
    const juce::Colour faint       { 0xff55555f };
    const juce::Colour value       { 0xffa6ecff };   // cyan value text
    const juce::Colour valueBox    { 0xff2b2c33 };
    const juce::Colour yellow      { 0xffe9b934 };
    const juce::Colour yellowDark  { 0xff3a3016 };
    const juce::Colour orange      { 0xffff8b33 };
    const juce::Colour green       { 0xff5fe07c };
    const juce::Colour red         { 0xffff5f5f };
    const juce::Colour lfo         { 0xffd57cf6 };
    const juce::Colour random      { 0xff7ef08c };
    const juce::Colour env         { 0xff69e4cd };
    const juce::Colour macro       { 0xff4fe0e6 };
    const juce::Colour blend       { 0xff7fb6ff };
    const juce::Colour pink        { 0xfff05be0 };
    const juce::Colour logoA       { 0xffd86cf8 };
    const juce::Colour logoB       { 0xff3fd5ff };

    inline juce::Font mono (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font sans (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    inline juce::String signedValue (float v)   // +15.00 / -7.50
    {
        return (v >= 0.0f ? "+" : "") + juce::String (v, 2);
    }
}

//==============================================================================
class BigModLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BigModLookAndFeel()
    {
        setColourScheme ({ Theme::bg, Theme::panel, Theme::panel, Theme::border, Theme::text,
                           Theme::panelTop, Theme::text, Theme::value.withAlpha (0.25f), Theme::text });

        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff0f1013));
        setColour (juce::ComboBox::textColourId, Theme::value);
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff4b7880));
        setColour (juce::ComboBox::arrowColourId, Theme::value);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff121216));
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::headerTextColourId, Theme::dim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff27414a));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextButton::buttonColourId, Theme::panelTop);
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f1013));
        setColour (juce::TextEditor::textColourId, Theme::value);
        setColour (juce::TextEditor::outlineColourId, Theme::border);
        setColour (juce::TextEditor::focusedOutlineColourId, Theme::value);
        setColour (juce::Label::textColourId, Theme::text);
        setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0f0f12));
        setColour (juce::ListBox::outlineColourId, Theme::border);
        setColour (juce::TableHeaderComponent::backgroundColourId, Theme::panel);
        setColour (juce::TableHeaderComponent::textColourId, Theme::text);
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3c3c45));
        setColour (juce::ToggleButton::textColourId, Theme::text);
        setColour (juce::ToggleButton::tickColourId, Theme::value);
        setColour (juce::ToggleButton::tickDisabledColourId, Theme::dim);
        setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
        setColour (juce::AlertWindow::textColourId, Theme::text);
        setColour (juce::AlertWindow::outlineColourId, Theme::border);
        setColour (juce::ResizableWindow::backgroundColourId, Theme::bg);
        setColour (juce::DocumentWindow::textColourId, Theme::text);
    }

    juce::Font getComboBoxFont (juce::ComboBox& b) override           { return Theme::mono (juce::jmin (13.0f, (float) b.getHeight() * 0.62f)); }
    juce::Font getPopupMenuFont() override                             { return Theme::mono (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override   { return Theme::mono (juce::jmin (13.0f, (float) h * 0.55f)); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId).withAlpha (box.isMouseOver (true) ? 1.0f : 0.7f));
        g.drawRoundedRectangle (r, 4.0f, 1.2f);

        juce::Path arrow;
        const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
        arrow.startNewSubPath (ax - 3.5f, ay - 1.5f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 3.5f, ay - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 18, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour ((down ? Theme::border : over ? Theme::panelTop.brighter (0.15f) : Theme::panelTop));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Theme::border.brighter (0.2f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (Theme::border);
        g.drawRect (0, 0, w, h);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool) override
    {
        g.setColour (juce::Colour (0xff1c1c21));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f), 3.0f);
        auto t = vertical ? juce::Rectangle<float> ((float) x, (float) thumbStart, (float) w, (float) thumbSize)
                          : juce::Rectangle<float> ((float) thumbStart, (float) y, (float) thumbSize, (float) h);
        g.setColour (juce::Colour (over ? 0xff50505c : 0xff3a3a44));
        g.fillRoundedRectangle (t.reduced (1.0f), 3.0f);
    }
};
