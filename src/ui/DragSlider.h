// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui
{

/** A slider that moves with the mouse, never jumps to it.

    - Clicking doesn't change the value; dragging moves the handle exactly as far as the
      mouse moved (at any zoom), so it stays under the cursor. Past an end it waits there
      until the cursor comes back.
    - Hold Shift while dragging for fine control (a tenth of the speed).
    - Rotary knobs have no track to follow, so they take a fixed drag distance.
*/
class DragSlider : public juce::Slider
{
public:
    DragSlider();

    /** Mouse movement (in this component's pixels) for a full sweep. */
    virtual int getDragLength() const;

    static constexpr double fineFactor = 0.1;
    static constexpr int rotaryDragLength = 200;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::Point<float> lastMouse, virtualMouse;
};

} // namespace spm::ui
