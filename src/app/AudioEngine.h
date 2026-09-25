// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/EngineCore.h"
#include "graph/GraphBuilder.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace spm::app
{

/** Connects the engine to a real (or simulated) audio device, and owns the graph builder.
    Message thread, apart from the device callbacks.
*/
class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /** Opens the saved device, or picks a sensible one (ASIO on Windows). Returns an error, if any. */
    juce::String initialise (const juce::XmlElement* savedState);
    std::unique_ptr<juce::XmlElement> createStateXml() const { return deviceManager.createStateXml(); }

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    engine::EngineCore& getCore() noexcept { return core; }
    graph::GraphBuilder& getBuilder() noexcept { return builder; }

    /** Builds the description and hands it to the audio thread. */
    void setGraph (const graph::GraphDesc& desc);

    struct Status
    {
        juce::String deviceName, typeName;
        bool running = false;
        double sampleRate = 0.0;
        int bufferSize = 0;
        int numInputs = 0, numOutputs = 0;
        int inputLatency = 0, outputLatency = 0;  // samples, as the driver reports
        int driverXruns = -1;                     // -1 when the driver can't tell
        engine::EngineStats stats;
    };

    Status getStatus();

    /** Zeroes the late / overload / driver xrun counts. */
    void resetCounters();

    /** Called on the message thread after the device (or its settings) changed. */
    std::function<void()> onDeviceChanged;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void chooseDefaultDevice();

    juce::AudioDeviceManager deviceManager;
    engine::EngineCore core;
    graph::GraphBuilder builder;
    std::atomic<bool> running { false };
    std::atomic<int> xrunBaseline { 0 };
};

} // namespace spm::app
