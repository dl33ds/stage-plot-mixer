// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/Theme.h"

#include <SpmResources.h>

namespace spm::ui::theme
{

namespace
{

juce::Typeface::Ptr regularFace, mediumFace, semiBoldFace;

juce::Typeface::Ptr faceFor (Weight weight)
{
    switch (weight)
    {
        case Weight::medium:   return mediumFace;
        case Weight::semiBold: return semiBoldFace;
        case Weight::regular:  break;
    }

    return regularFace;
}

} // namespace

void loadFonts()
{
    if (regularFace != nullptr)
        return;

    regularFace = juce::Typeface::createSystemTypefaceFor (spm_resources::InterRegular_ttf, spm_resources::InterRegular_ttfSize);
    mediumFace = juce::Typeface::createSystemTypefaceFor (spm_resources::InterMedium_ttf, spm_resources::InterMedium_ttfSize);
    semiBoldFace = juce::Typeface::createSystemTypefaceFor (spm_resources::InterSemiBold_ttf, spm_resources::InterSemiBold_ttfSize);
}

void unloadFonts()
{
    // Held until exit, these would outlive JUCE's typeface cache and crash on quit.
    regularFace = mediumFace = semiBoldFace = nullptr;
}

juce::Font font (float size, Weight weight, bool tabular)
{
    const auto face = faceFor (weight);
    auto options = (face != nullptr ? juce::FontOptions (face) : juce::FontOptions()).withHeight (size);

    if (tabular)
        options = options.withFeatureEnabled ("tnum");

    return juce::Font (options);
}

juce::Colour categoryColour (const juce::String& category)
{
    if (category == "Sources")      return juce::Colour (0xff3ecf8e);
    if (category == "Destinations") return juce::Colour (0xffef8b5b);
    if (category == "Processing")   return juce::Colour (0xffb58cff);
    if (category == "Mixing")       return juce::Colour (0xff4c9aff);
    if (category == "Routing")      return juce::Colour (0xfff5b942);
    if (category == "Analysis")     return juce::Colour (0xff5bd0ef);
    return textMuted;
}

juce::Colour channelColour (int channels)
{
    if (channels <= 1) return juce::Colour (0xff9aa7bd);
    if (channels == 2) return juce::Colour (0xff4c9aff);
    return juce::Colour (0xffb58cff);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    loadFonts();

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, surfaceHigh);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, surfaceHigh);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, surface);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, surface);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, text);
    setColour (juce::ComboBox::backgroundColourId, surfaceHigh);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, textMuted);
    setColour (juce::PopupMenu::backgroundColourId, surface);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::ListBox::backgroundColourId, surface);
    setColour (juce::ListBox::textColourId, text);
    setColour (juce::ScrollBar::thumbColourId, border);
    setColour (juce::AlertWindow::backgroundColourId, surface);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, border);
    setColour (juce::TooltipWindow::backgroundColourId, surfaceHigh);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, border);
    setColour (juce::GroupComponent::outlineColourId, border);
    setColour (juce::GroupComponent::textColourId, textMuted);

    getCurrentColourScheme().setUIColour (ColourScheme::windowBackground, background);
    getCurrentColourScheme().setUIColour (ColourScheme::widgetBackground, surfaceHigh);
    getCurrentColourScheme().setUIColour (ColourScheme::menuBackground, surface);
    getCurrentColourScheme().setUIColour (ColourScheme::outline, border);
    getCurrentColourScheme().setUIColour (ColourScheme::defaultText, text);
    getCurrentColourScheme().setUIColour (ColourScheme::defaultFill, accent);
    getCurrentColourScheme().setUIColour (ColourScheme::highlightedText, text);
    getCurrentColourScheme().setUIColour (ColourScheme::highlightedFill, accent.withAlpha (0.35f));
    getCurrentColourScheme().setUIColour (ColourScheme::menuText, text);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    const auto style = f.getTypefaceStyle();

    if (f.isBold() || style.containsIgnoreCase ("semibold"))
        return semiBoldFace;

    if (style.containsIgnoreCase ("medium"))
        return mediumFace;

    return regularFace != nullptr ? regularFace : juce::LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int height)
{
    return font (juce::jmin (14.0f, (float) height * 0.5f), Weight::medium);
}

juce::Font LookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (13.0f); }
juce::Font LookAndFeel::getPopupMenuFont() { return font (14.0f); }
juce::Font LookAndFeel::getAlertWindowMessageFont() { return font (14.0f); }
juce::Font LookAndFeel::getAlertWindowTitleFont() { return font (17.0f, Weight::semiBold); }

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                        bool highlighted, bool down)
{
    auto fill = button.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : colour;
    if (down)
        fill = fill.darker (0.2f);
    else if (highlighted)
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (button.getLocalBounds().toFloat(), radius);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // A small switch followed by the label.
    auto area = button.getLocalBounds().toFloat();
    const auto h = 16.0f;
    auto track = area.removeFromLeft (28.0f).withSizeKeepingCentre (28.0f, h);
    const auto on = button.getToggleState();

    g.setColour (on ? accent : (highlighted ? border.brighter (0.1f) : border));
    g.fillRoundedRectangle (track, h / 2.0f);

    g.setColour (juce::Colours::white.withAlpha (button.isEnabled() ? 1.0f : 0.5f));
    const auto knob = h - 4.0f;
    g.fillEllipse (on ? track.getRight() - knob - 2.0f : track.getX() + 2.0f, track.getY() + 2.0f, knob, knob);

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (font (13.0f));
        g.drawText (button.getButtonText(), area.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
    }
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                    float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }

    const auto horizontal = style == juce::Slider::LinearHorizontal;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto thickness = 4.0f;
    const auto track = horizontal ? bounds.withSizeKeepingCentre (bounds.getWidth(), thickness)
                                  : bounds.withSizeKeepingCentre (thickness, bounds.getHeight());

    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, thickness / 2.0f);

    // Fill from the default ("zero") point for bipolar ranges like pan, otherwise from the start.
    const auto range = slider.getRange();
    const auto origin = range.getStart() < 0.0 && range.getEnd() > 0.0 ? (float) slider.getPositionOfValue (0.0)
                                                                        : (horizontal ? (float) x : (float) (y + height));
    auto filled = horizontal ? juce::Rectangle<float>::leftTopRightBottom (juce::jmin (origin, sliderPos), track.getY(),
                                                                           juce::jmax (origin, sliderPos), track.getBottom())
                             : juce::Rectangle<float>::leftTopRightBottom (track.getX(), juce::jmin (origin, sliderPos),
                                                                           track.getRight(), juce::jmax (origin, sliderPos));

    g.setColour (slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (filled, thickness / 2.0f);

    const auto thumb = 12.0f;
    const auto centre = horizontal ? juce::Point<float> (sliderPos, bounds.getCentreY())
                                   : juce::Point<float> (bounds.getCentreX(), sliderPos);
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre (centre));
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (area, radius);

    if (box.hasKeyboardFocus (true))
    {
        g.setColour (accent);
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }

    juce::Path arrow;
    const auto cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height)
{
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (surfaceHigh);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (border);
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (text);
    g.setFont (font (13.0f));
    g.drawFittedText (tip, area.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto f = font (13.0f);
    const auto w = juce::jmin (360, juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, tip)) + 18);
    const auto lines = juce::jmax (1, juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, tip)) / 340 + 1);
    const auto h = lines * 17 + 10;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

} // namespace spm::ui::theme
