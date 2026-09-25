// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::engine { class MeterChannel; }

namespace spm::ui
{

/** A vertical peak + RMS meter with peak hold and a clip light (click to clear).
    The owner calls update() regularly with the channel to read (or nullptr).
*/
class LevelMeter final : public juce::Component
{
public:
    void update (engine::MeterChannel* channel, double secondsSinceLast);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr float minDb = -60.0f;

private:
    float toProportion (float db) const noexcept;

    engine::MeterChannel* source = nullptr;
    float peakDb = minDb, rmsDb = minDb, holdDb = minDb;
    double holdAge = 0.0;
    bool clipped = false;
};

} // namespace spm::ui
