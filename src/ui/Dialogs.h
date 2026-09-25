// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui
{

/** Asks for a line of text (a name). onDone runs with the trimmed text, unless cancelled
    or left empty.
*/
void askForText (juce::Component* parent, const juce::String& title, const juce::String& message, const juce::String& initial,
                 const juce::String& okText, std::function<void (const juce::String&)> onDone);

} // namespace spm::ui
