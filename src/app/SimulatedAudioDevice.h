// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace spm::app
{

/** A device type offering one virtual 32-in / 32-out interface, so the mixer can be
    tried and tested without hardware. Its inputs carry quiet test tones (a different
    pitch per channel); its outputs go nowhere. Callbacks are timed by a thread.
*/
class SimulatedAudioDeviceType final : public juce::AudioIODeviceType
{
public:
    SimulatedAudioDeviceType();

    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool wantInputNames) const override;
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* device, bool) const override;
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& outputDeviceName, const juce::String& inputDeviceName) override;

    static constexpr const char* typeName = "Simulated";
    static constexpr const char* deviceName = "Virtual 32-channel test device";
    static constexpr int numChannels = 32;
};

} // namespace spm::app
