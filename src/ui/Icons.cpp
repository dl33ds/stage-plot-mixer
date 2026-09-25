// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/Icons.h"

#include "ui/Theme.h"

#include <SpmResources.h>

#include <map>

namespace spm::ui
{

namespace
{

juce::String findSvg (const juce::String& name)
{
    const auto file = name + ".svg";

    for (int i = 0; i < spm_resources::namedResourceListSize; ++i)
    {
        if (file == spm_resources::originalFilenames[i])
        {
            int size = 0;
            const auto* data = spm_resources::getNamedResource (spm_resources::namedResourceList[i], size);
            return juce::String::fromUTF8 (data, size);
        }
    }

    return {};
}

} // namespace

std::unique_ptr<juce::Drawable> createIcon (const juce::String& name, juce::Colour colour)
{
    static std::map<juce::String, juce::String> cache;

    auto& svg = cache[name];
    if (svg.isEmpty())
        svg = findSvg (name);

    if (svg.isEmpty())
        return nullptr;

    const auto coloured = svg.replace ("currentColor", "#" + colour.toDisplayString (false));
    return juce::Drawable::createFromSVGString (coloured);
}

void drawIcon (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour)
{
    if (auto icon = createIcon (name, colour))
        icon->drawWithin (g, area, juce::RectanglePlacement::centred, colour.getFloatAlpha());
}

//==============================================================================
IconButton::IconButton (const juce::String& iconName, const juce::String& tooltip, const juce::String& text)
    : juce::Button (tooltip), icon (iconName), label (text)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIcon (const juce::String& iconName)
{
    icon = iconName;
    repaint();
}

void IconButton::setLabel (const juce::String& text)
{
    label = text;
    repaint();
}

void IconButton::setHighlightColour (std::optional<juce::Colour> colour)
{
    highlight = colour;
    repaint();
}

int IconButton::getIdealWidth (int height) const
{
    auto width = height;
    if (label.isNotEmpty())
        width += juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (13.0f, theme::Weight::medium), label)) + 12;
    return width;
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto area = getLocalBounds().toFloat();

    auto fill = highlight.value_or (juce::Colours::transparentBlack);
    if (down)
        fill = highlight ? fill.darker (0.2f) : theme::border;
    else if (highlighted)
        fill = highlight ? fill.brighter (0.1f) : theme::surfaceHigh;

    g.setColour (fill);
    g.fillRoundedRectangle (area, theme::radius);

    const auto colour = isEnabled() ? theme::text : theme::textMuted.withAlpha (0.4f);
    const auto iconSize = juce::jmin (18.0f, area.getHeight() - 8.0f);
    auto iconArea = area.removeFromLeft (area.getHeight()).withSizeKeepingCentre (iconSize, iconSize);

    if (label.isEmpty())
        iconArea = getLocalBounds().toFloat().withSizeKeepingCentre (iconSize, iconSize);

    drawIcon (g, icon, iconArea, colour);

    if (label.isNotEmpty())
    {
        g.setColour (colour);
        g.setFont (theme::font (13.0f, theme::Weight::medium));
        g.drawText (label, area.withTrimmedRight (6.0f), juce::Justification::centredLeft, false);
    }
}

} // namespace spm::ui
