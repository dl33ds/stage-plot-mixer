// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "app/AudioEngine.h"
#include "ui/Icons.h"

namespace spm::app
{

/** Bottom bar: device, format, CPU and the dropout counters, with Copy report for testing. */
class StatusBar final : public juce::Component, private juce::Timer
{
public:
    explicit StatusBar (AudioEngine& engine);

    /** Extra lines for the report (e.g. what's in the session). */
    std::function<juce::String()> getReportExtras;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int height = 30;

private:
    void timerCallback() override;
    juce::String makeReport();

    AudioEngine& engine;
    juce::String device, format, cpu, counters;
    bool problems = false;
    double countersSince = 0.0;
    float peakCpu = 0.0f;

    ui::IconButton resetButton { "undo-2", "Start counting dropouts again from zero", "Reset counters" },
        reportButton { "copy", "Copy a test report to the clipboard, to paste into an email or message", "Copy report" };
};

} // namespace spm::app
