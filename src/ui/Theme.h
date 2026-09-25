// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui::theme
{

// Dark, flat palette. Meters use green → amber → red at -18 / -6 dBFS.
inline const juce::Colour background  { 0xff16181c };
inline const juce::Colour surface     { 0xff1f2228 };
inline const juce::Colour surfaceHigh { 0xff2a2e36 };
inline const juce::Colour border      { 0xff353a44 };
inline const juce::Colour text        { 0xffe6e8ec };
inline const juce::Colour textMuted   { 0xff9aa1ad };
inline const juce::Colour accent      { 0xff4c9aff };
inline const juce::Colour good        { 0xff3ecf8e };
inline const juce::Colour warning     { 0xfff5b942 };
inline const juce::Colour danger      { 0xffef5b5b };

inline constexpr int grid = 4;
inline constexpr float radius = 6.0f;

} // namespace spm::ui::theme
