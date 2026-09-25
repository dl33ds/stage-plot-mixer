// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "app/AudioEngine.h"
#include "ui/LevelMeter.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::app
{

/** Phase 1 window content: a fixed test graph with meters for every input and output,
    a monitor mix of all inputs to outputs 1–2, a test tone, and the engine status bar.
    The node editor replaces the middle part in Phase 2.
*/
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AudioEngine& engine);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildGraph();
    void pushParameters();
    void showAudioSettings();
    void copyReport();
    juce::String makeReport();

    struct Strip
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<ui::LevelMeter> meter;
        graph::NodeId node = 0;
        int channel = 0;
        bool isOutput = false;
    };

    AudioEngine& engine;
    std::vector<Strip> strips;
    int numInputs = 0, numOutputs = 0;

    juce::Label title, inputsHeading, outputsHeading;
    juce::ToggleButton monitorToggle { "Monitor all inputs on outputs 1-2" }, toneToggle { "Test tone (1 kHz)" };
    juce::Slider monitorLevel;
    juce::Label monitorLevelLabel;

    juce::Label statusDevice, statusFormat, statusCpu, statusCounters;
    juce::TextButton settingsButton { "Audio settings..." }, resetButton { "Reset counters" },
                     reportButton { "Copy report" }, muteButton;

    double lastTick = 0.0, countersSince = 0.0;
    float peakCpu = 0.0f;
};

} // namespace spm::app
