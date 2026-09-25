// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui::theme
{

// Dark, flat palette. Meters use green → amber → red at -18 / -6 dBFS.
inline const juce::Colour background  { 0xff16181c };
inline const juce::Colour canvas      { 0xff121417 };
inline const juce::Colour surface     { 0xff1f2228 };
inline const juce::Colour surfaceHigh { 0xff2a2e36 };
inline const juce::Colour border      { 0xff353a44 };
inline const juce::Colour text        { 0xffe6e8ec };
inline const juce::Colour textMuted   { 0xff9aa1ad };
inline const juce::Colour accent      { 0xff4c9aff };
inline const juce::Colour good        { 0xff3ecf8e };
inline const juce::Colour warning     { 0xfff5b942 };
inline const juce::Colour danger      { 0xffef5b5b };

inline constexpr int grid = 4;         // spacing unit
inline constexpr float radius = 6.0f;  // corner radius

/** Colour for a node category (the strip at the top of a node). */
juce::Colour categoryColour (const juce::String& category);

/** Colour for a port or wire carrying this many channels. */
juce::Colour channelColour (int channels);

enum class Weight { regular, medium, semiBold };

/** Inter at a size (in pixels) and weight. tabular: digits all the same width, for
    numbers that change (levels, counters).
*/
juce::Font font (float size, Weight weight = Weight::regular, bool tabular = false);

/** Loads the bundled fonts; call once at startup before creating components. */
void loadFonts();

/** Releases the bundled fonts; call at shutdown after every component is gone. */
void unloadFonts();

/** The app's look and feel: Inter everywhere, flat dark controls. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 7; }
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                       int buttonH, juce::ComboBox&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};

} // namespace spm::ui::theme
