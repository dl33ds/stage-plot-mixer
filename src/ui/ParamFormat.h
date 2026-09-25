// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "nodes/NodeTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui
{

/** A parameter value as the user sees it: "-12.0 dB", "-inf dB", "1.00 kHz", "L 30". */
juce::String formatParam (const nodes::ParamSpec& spec, float value);

/** Parses typed text back to a value (units optional). */
float parseParam (const nodes::ParamSpec& spec, const juce::String& text);

/** Sets range, feel (fader law for levels, log for frequency) and text for a slider
    that edits a continuous or integer parameter.
*/
void configureSlider (juce::Slider& slider, const nodes::ParamSpec& spec);

/** Gain in dB for wires: -inf .. +12. */
void configureWireGainSlider (juce::Slider& slider);

} // namespace spm::ui
