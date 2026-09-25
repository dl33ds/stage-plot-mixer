// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "DeviceSurvey.h"

namespace spm::diag
{

struct TestSettings
{
    double sampleRate = 48000.0;
    int bufferSize = 0;          // 0 = device default
    double seconds = 60.0;       // stability test duration
    int inputChannel = 1;        // latency test, 1-based
    int outputChannel = 1;       // latency test, 1-based
    float probeLevelDb = -12.0f; // latency test signal level
    int latencyRuns = 5;
};

enum class TestResult
{
    pass,
    warning,
    fail
};

juce::String toString (TestResult r);

/** Runs the device with silent outputs, measuring callback regularity, dropouts and input levels. */
TestResult runStabilityTest (Console&, juce::AudioIODeviceType&, const DeviceChoice&, const TestSettings&);

/** Measures round-trip latency using a loopback cable from an output to an input. */
TestResult runLatencyTest (Console&, juce::AudioIODeviceType&, const DeviceChoice&, const TestSettings&);

} // namespace spm::diag
