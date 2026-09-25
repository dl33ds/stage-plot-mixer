// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Console.h"

namespace spm::diag
{

/** One device as seen by one driver type. For driver types with separate input and
    output devices (WASAPI, CoreAudio) a physical device may appear twice.
*/
struct DeviceEntry
{
    juce::String typeName;
    juce::String name;
    bool isInput = false;
    bool isOutput = false;
    bool separateInputsAndOutputs = false;

    juce::StringArray inputChannels, outputChannels;
    juce::Array<double> sampleRates;
    juce::Array<int> bufferSizes;
    int defaultBufferSize = 0;
    juce::String error;

    /** False for drivers that are installed but have no hardware behind them. */
    bool isUsable() const noexcept { return error.isEmpty() && (inputChannels.size() + outputChannels.size()) > 0; }

    /** True for Windows paths that go through the Windows mixer, with a fixed ~10 ms buffer. */
    bool isSharedMode() const noexcept { return typeName == "Windows Audio" || typeName == "DirectSound"; }

    juce::String describe() const;
};

/** Which device(s) to open for a test. */
struct DeviceChoice
{
    juce::String typeName;
    juce::String inputDevice;   // empty = no input
    juce::String outputDevice;  // empty = no output

    juce::String describe() const;
};

class DeviceSurvey
{
public:
    DeviceSurvey();

    /** Rescans all driver types and probes every device. Slow (loads each ASIO driver). */
    void scan();

    /** Sorted best-first: native low-latency drivers (ASIO, CoreAudio, ALSA), then WASAPI
        exclusive, then shared-mode paths. Unusable entries come last.
    */
    const juce::Array<DeviceEntry>& getEntries() const noexcept { return entries; }

    /** Index of the entry a tester should normally pick, or -1 if none is usable. */
    int getRecommendedIndex() const;

    juce::AudioIODeviceType* findType (const juce::String& typeName) const;

    const DeviceEntry* findEntry (const juce::String& typeName, const juce::String& name, bool input) const;

    void print (Console& console) const;

    static void printSystemInfo (Console& console);

private:
    juce::OwnedArray<juce::AudioIODeviceType> types;
    juce::Array<DeviceEntry> entries;
};

juce::String describeRates (const juce::Array<double>& rates);
juce::String describeBuffers (const juce::Array<int>& sizes, int defaultSize);

} // namespace spm::diag
