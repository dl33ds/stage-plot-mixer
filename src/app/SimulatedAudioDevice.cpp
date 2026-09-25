// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/SimulatedAudioDevice.h"

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

namespace spm::app
{

namespace
{

juce::StringArray channelNames (const char* prefix)
{
    juce::StringArray names;
    for (int i = 1; i <= SimulatedAudioDeviceType::numChannels; ++i)
        names.add (juce::String (prefix) + " " + juce::String (i));
    return names;
}

class SimulatedAudioDevice final : public juce::AudioIODevice, private juce::Thread
{
public:
    SimulatedAudioDevice()
        : juce::AudioIODevice (SimulatedAudioDeviceType::deviceName, SimulatedAudioDeviceType::typeName),
          juce::Thread ("Simulated audio")
    {
    }

    ~SimulatedAudioDevice() override { close(); }

    juce::StringArray getOutputChannelNames() override { return channelNames ("Out"); }
    juce::StringArray getInputChannelNames() override { return channelNames ("In"); }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0, 88200.0, 96000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 32, 64, 128, 256, 512, 1024, 2048 }; }
    int getDefaultBufferSize() override { return 128; }

    juce::String open (const juce::BigInteger& inputs, const juce::BigInteger& outputs, double rate, int bufferSize) override
    {
        close();
        activeInputs = inputs;
        activeInputs.setRange (numChannels, activeInputs.getHighestBit() + 1, false);
        activeOutputs = outputs;
        activeOutputs.setRange (numChannels, activeOutputs.getHighestBit() + 1, false);
        sampleRate = rate > 0.0 ? rate : 48000.0;
        blockSize = bufferSize > 0 ? bufferSize : getDefaultBufferSize();
        opened = true;
        return {};
    }

    void close() override
    {
        stop();
        opened = false;
    }

    bool isOpen() override { return opened; }

    void start (juce::AudioIODeviceCallback* newCallback) override
    {
        if (! opened || newCallback == nullptr)
            return;

        stop();
        callback = newCallback;
        callback->audioDeviceAboutToStart (this);
        startThread (juce::Thread::Priority::highest);
    }

    void stop() override
    {
        if (callback == nullptr)
            return;

        stopThread (2000);
        auto* old = callback;
        callback = nullptr;
        old->audioDeviceStopped();
    }

    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOutputs; }
    juce::BigInteger getActiveInputChannels() const override { return activeInputs; }
    int getOutputLatencyInSamples() override { return blockSize; }
    int getInputLatencyInSamples() override { return blockSize; }

private:
    void run() override
    {
        const auto numIns = activeInputs.countNumberOfSetBits();
        const auto numOuts = activeOutputs.countNumberOfSetBits();

        juce::AudioBuffer<float> ins (std::max (1, numIns), blockSize), outs (std::max (1, numOuts), blockSize);
        std::vector<int> inputIndex;  // device channel for each active input
        for (int ch = 0; ch < numChannels; ++ch)
            if (activeInputs[ch])
                inputIndex.push_back (ch);

        std::vector<double> phases ((size_t) numChannels, 0.0);
        const auto period = std::chrono::duration<double> (blockSize / sampleRate);
        auto next = std::chrono::steady_clock::now();

        while (! threadShouldExit())
        {
            // Channel k: a tone at 110 Hz × (k + 1), at about -30 dBFS.
            for (int i = 0; i < numIns; ++i)
            {
                const auto ch = inputIndex[(size_t) i];
                const auto increment = 110.0 * (ch + 1) / sampleRate;
                auto& phase = phases[(size_t) ch];
                auto* data = ins.getWritePointer (i);

                for (int s = 0; s < blockSize; ++s)
                {
                    data[s] = 0.03f * (float) std::sin (2.0 * std::numbers::pi * phase);
                    phase += increment;
                    phase -= std::floor (phase);
                }
            }

            outs.clear();
            callback->audioDeviceIOCallbackWithContext (ins.getArrayOfReadPointers(), numIns, outs.getArrayOfWritePointers(),
                                                        numOuts, blockSize, {});

            next += std::chrono::duration_cast<std::chrono::steady_clock::duration> (period);
            const auto now = std::chrono::steady_clock::now();

            if (next < now - std::chrono::milliseconds (100))
                next = now;  // fell far behind (e.g. debugger): don't try to catch up
            else
                std::this_thread::sleep_until (next);
        }
    }

    static constexpr int numChannels = SimulatedAudioDeviceType::numChannels;

    juce::BigInteger activeInputs, activeOutputs;
    double sampleRate = 48000.0;
    int blockSize = 128;
    bool opened = false;
    juce::AudioIODeviceCallback* callback = nullptr;
};

} // namespace

SimulatedAudioDeviceType::SimulatedAudioDeviceType() : juce::AudioIODeviceType (typeName) {}

juce::StringArray SimulatedAudioDeviceType::getDeviceNames (bool) const
{
    return { deviceName };
}

int SimulatedAudioDeviceType::getIndexOfDevice (juce::AudioIODevice* device, bool) const
{
    return device != nullptr && device->getName() == deviceName ? 0 : -1;
}

juce::AudioIODevice* SimulatedAudioDeviceType::createDevice (const juce::String& outputDeviceName, const juce::String& inputDeviceName)
{
    if (outputDeviceName == deviceName || inputDeviceName == deviceName)
        return new SimulatedAudioDevice();

    return nullptr;
}

} // namespace spm::app
