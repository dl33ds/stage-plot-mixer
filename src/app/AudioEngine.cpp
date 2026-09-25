// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/AudioEngine.h"

#include "app/SimulatedAudioDevice.h"

namespace spm::app
{

AudioEngine::AudioEngine()
{
    // Create the platform's driver types (ASIO, Windows Audio...) first: JUCE only adds them
    // while its list is empty, so adding the simulated type first would hide all hardware.
    deviceManager.getAvailableDeviceTypes();
    deviceManager.addAudioDeviceType (std::make_unique<SimulatedAudioDeviceType>());
    deviceManager.addChangeListener (this);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

juce::String AudioEngine::initialise (const juce::XmlElement* savedState)
{
    const auto maxChannels = engine::EngineCore::maxDeviceChannels;

    // The simulated device is for trying things out; start on real hardware when there is some.
    if (savedState != nullptr && savedState->getStringAttribute ("deviceType") == SimulatedAudioDeviceType::typeName)
        savedState = nullptr;

    auto error = deviceManager.initialise (maxChannels, maxChannels, savedState, savedState == nullptr);

    if (savedState == nullptr || deviceManager.getCurrentAudioDevice() == nullptr)
        chooseDefaultDevice();

    deviceManager.addAudioCallback (this);
    startTimerHz (20);
    return deviceManager.getCurrentAudioDevice() != nullptr ? juce::String() : error;
}

void AudioEngine::chooseDefaultDevice()
{
    // Prefer a real ASIO driver: wrapper drivers and shared-mode Windows audio are the
    // usual cause of dropouts (see docs/TESTING.md).
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != "ASIO")
            continue;

        type->scanForDevices();

        for (const auto& name : type->getDeviceNames())
        {
            const auto lower = name.toLowerCase();
            if (lower.contains ("asio4all") || lower.contains ("fl studio") || lower.contains ("generic low latency")
                || lower.contains ("asio2wasapi"))
                continue;

            deviceManager.setCurrentAudioDeviceType ("ASIO", true);
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.outputDeviceName = setup.inputDeviceName = name;
            setup.sampleRate = 48000.0;
            setup.bufferSize = 128;
            setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;

            if (deviceManager.setAudioDeviceSetup (setup, true).isEmpty())
                return;
        }
    }

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        // No hardware at all: the simulated device always works.
        deviceManager.setCurrentAudioDeviceType (SimulatedAudioDeviceType::typeName, true);
    }
}

void AudioEngine::setGraph (const graph::GraphDesc& desc)
{
    core.submit (builder.build (desc));
}

AudioEngine::Status AudioEngine::getStatus()
{
    Status s;
    s.stats = core.takeStats();
    s.running = running.load();

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        s.deviceName = device->getName();
        s.typeName = device->getTypeName();
        s.sampleRate = device->getCurrentSampleRate();
        s.bufferSize = device->getCurrentBufferSizeSamples();
        s.numInputs = device->getActiveInputChannels().countNumberOfSetBits();
        s.numOutputs = device->getActiveOutputChannels().countNumberOfSetBits();
        s.inputLatency = device->getInputLatencyInSamples();
        s.outputLatency = device->getOutputLatencyInSamples();

        const auto xruns = device->getXRunCount();
        s.driverXruns = xruns >= 0 ? xruns - xrunBaseline : -1;
    }

    return s;
}

void AudioEngine::resetCounters()
{
    core.resetStats();

    if (auto* device = deviceManager.getCurrentAudioDevice())
        xrunBaseline = std::max (0, device->getXRunCount());
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                                    int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    core.process (inputs, numInputs, outputs, numOutputs, numSamples, engine::monotonicSeconds());
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    core.prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
                  device->getActiveInputChannels().countNumberOfSetBits(),
                  device->getActiveOutputChannels().countNumberOfSetBits());
    xrunBaseline = std::max (0, device->getXRunCount());
    running = true;
}

void AudioEngine::audioDeviceStopped()
{
    running = false;
}

void AudioEngine::audioDeviceError (const juce::String& message)
{
    juce::ignoreUnused (message);
    DBG ("Audio device error: " << message);
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (onDeviceChanged)
        onDeviceChanged();
}

void AudioEngine::timerCallback()
{
    core.collectGarbage();

    if (builder.pruneFinishedFades (running.load()))
        core.submit (builder.rebuild());
}

} // namespace spm::app
