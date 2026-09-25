// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui
{

/** Lucide icons, bundled as SVG. Returns nullptr for unknown names. */
std::unique_ptr<juce::Drawable> createIcon (const juce::String& name, juce::Colour colour);

/** Draws an icon fitted into an area. */
void drawIcon (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour);

/** A flat button showing an icon (and optional text), with a tooltip. */
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& iconName, const juce::String& tooltip, const juce::String& text = {});

    void setIcon (const juce::String& iconName);
    void setLabel (const juce::String& text);
    void setHighlightColour (std::optional<juce::Colour> colour);
    void setIconColour (std::optional<juce::Colour> colour);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    /** Width needed for the icon plus text at the given height. */
    int getIdealWidth (int height) const;

private:
    juce::String icon, label;
    std::optional<juce::Colour> highlight, iconColour;
};

} // namespace spm::ui
