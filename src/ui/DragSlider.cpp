// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/DragSlider.h"

namespace spm::ui
{

DragSlider::DragSlider()
{
    setSliderSnapsToMousePosition (false);
    // No velocity mode, so no modifier key changes how a drag feels (except Shift, below).
    setVelocityModeParameters (1.0, 1, 0.0, false);
    setRepaintsOnMouseActivity (true);
}

int DragSlider::getDragLength() const
{
    if (isRotary())
        return rotaryDragLength;

    // The area the handle travels over, as the look and feel lays it out.
    auto& lf = getLookAndFeel();
    const auto bounds = lf.getSliderLayout (const_cast<DragSlider&> (*this)).sliderBounds;
    return isHorizontal() ? bounds.getWidth() : bounds.getHeight();
}

void DragSlider::resized()
{
    juce::Slider::resized();
    setMouseDragSensitivity (std::max (1, getDragLength()));
    setMouseCursor (isHorizontal() || getSliderStyle() == RotaryHorizontalDrag ? juce::MouseCursor::LeftRightResizeCursor
                                                                             : juce::MouseCursor::UpDownResizeCursor);
}

void DragSlider::mouseDown (const juce::MouseEvent& e)
{
    setMouseDragSensitivity (std::max (1, getDragLength()));
    lastMouse = virtualMouse = e.position;
    juce::Slider::mouseDown (e);
}

void DragSlider::mouseDrag (const juce::MouseEvent& e)
{
    // The slider sees a mouse that moves slower while Shift is held; the handle
    // follows it from where it was, without jumping.
    const auto delta = e.position - lastMouse;
    lastMouse = e.position;
    virtualMouse += delta * (e.mods.isShiftDown() ? (float) fineFactor : 1.0f);
    juce::Slider::mouseDrag (e.withNewPosition (virtualMouse));
}

} // namespace spm::ui
