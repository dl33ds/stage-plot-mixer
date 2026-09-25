// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/WindowPlacement.h"

namespace spm::model
{

juce::Rectangle<int> placeWindow (juce::Rectangle<int> saved, juce::Rectangle<int> savedDisplay,
                                  const juce::Array<juce::Rectangle<int>>& displays)
{
    if (displays.isEmpty())
        return saved;

    auto target = displays.getFirst();
    auto window = saved;

    if (displays.contains (savedDisplay))
    {
        target = savedDisplay;
    }
    else
    {
        auto bestOverlap = 0;
        for (const auto& d : displays)
            if (const auto area = d.getIntersection (saved); area.getWidth() * area.getHeight() > bestOverlap)
            {
                bestOverlap = area.getWidth() * area.getHeight();
                target = d;
            }

        // Off every display now: same place relative to the display's corner.
        if (bestOverlap == 0 && ! savedDisplay.isEmpty())
            window.setPosition (target.getPosition() + (saved.getPosition() - savedDisplay.getPosition()));
    }

    window.setSize (juce::jmin (window.getWidth(), target.getWidth()), juce::jmin (window.getHeight(), target.getHeight()));
    return window.constrainedWithin (target);
}

} // namespace spm::model
