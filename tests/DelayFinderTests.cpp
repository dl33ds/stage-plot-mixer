// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/DelayFinder.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace spm;

namespace
{

/** Simulates a loopback: silence, then the probe scaled by gain, plus background noise. */
std::vector<float> simulateCapture (const std::vector<float>& probe, int delay, float gain, float noiseLevel, int length)
{
    const auto noise = makeProbeSignal (length, noiseLevel, 0xabcdefu);
    std::vector<float> out (noise);

    for (size_t i = 0; i < probe.size(); ++i)
        if (delay + (int) i < length)
            out[(size_t) delay + i] += gain * probe[i];

    return out;
}

} // namespace

TEST_CASE ("Probe signal is deterministic and bounded", "[delay]")
{
    const auto a = makeProbeSignal (4096, 0.5f);
    const auto b = makeProbeSignal (4096, 0.5f);

    REQUIRE (a == b);

    for (auto s : a)
        REQUIRE (std::abs (s) <= 0.5f);

    REQUIRE (makeProbeSignal (4096, 0.5f, 42u) != a);
}

TEST_CASE ("findDelay recovers the exact lag", "[delay]")
{
    const auto delay = GENERATE (0, 1, 37, 512, 12000);
    const auto probe = makeProbeSignal (2048, 1.0f);
    const auto capture = simulateCapture (probe, delay, 0.25f, 0.001f, 16000);

    const auto result = findDelay (probe, capture, 16000);

    REQUIRE (result.delaySamples == delay);
    REQUIRE (result.isReliable());
    REQUIRE_FALSE (result.inverted);
    REQUIRE (result.correlation > 0.9f);
}

TEST_CASE ("findDelay detects inverted polarity", "[delay]")
{
    const auto probe = makeProbeSignal (2048, 1.0f);
    const auto capture = simulateCapture (probe, 300, -0.5f, 0.001f, 8000);

    const auto result = findDelay (probe, capture, 8000);

    REQUIRE (result.delaySamples == 300);
    REQUIRE (result.inverted);
}

TEST_CASE ("findDelay survives low signal-to-noise", "[delay]")
{
    // Probe 20 dB below the noise floor: correlation gain over 8192 samples still finds it.
    const auto probe = makeProbeSignal (8192, 1.0f);
    const auto capture = simulateCapture (probe, 777, 0.01f, 0.1f, 20000);

    const auto result = findDelay (probe, capture, 20000);

    REQUIRE (result.delaySamples == 777);
    REQUIRE (result.peakRatio > 2.0f);
}

TEST_CASE ("findDelay reports no match for silence or unrelated noise", "[delay]")
{
    const auto probe = makeProbeSignal (2048, 1.0f);

    SECTION ("silence")
    {
        const std::vector<float> silence (8000, 0.0f);
        REQUIRE_FALSE (findDelay (probe, silence, 8000).isReliable());
    }

    SECTION ("unrelated noise")
    {
        const auto other = makeProbeSignal (8000, 0.5f, 999u);
        REQUIRE_FALSE (findDelay (probe, other, 8000).isReliable());
    }

    SECTION ("capture shorter than probe")
    {
        const std::vector<float> tooShort (100, 0.0f);
        REQUIRE (findDelay (probe, tooShort, 100).delaySamples == -1);
    }
}
