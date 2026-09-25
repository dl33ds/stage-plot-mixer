// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace spm::model
{

/** Where a saved window should go on the displays there are now.

    `savedDisplay` is the user area of the display it was on when saved; `displays` are
    today's user areas, the primary first. If that display is still there (the same
    area), the window goes back where it was. If not, it goes to the display it overlaps
    most, or the primary, at the same place relative to the display's corner. Either way
    it's shrunk to fit and moved fully onto the display, so a title bar is never lost
    off screen.
*/
juce::Rectangle<int> placeWindow (juce::Rectangle<int> saved, juce::Rectangle<int> savedDisplay,
                                  const juce::Array<juce::Rectangle<int>>& displays);

} // namespace spm::model
