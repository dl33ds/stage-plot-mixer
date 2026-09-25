// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/CallbackStats.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace spm;
using Catch::Approx;

namespace
{

struct Block
{
    explicit Block (int channels, int samples, float level = 0.0f)
        : data ((size_t) channels, std::vector<float> ((size_t) samples, level))
    {
        for (auto& ch : data)
            pointers.push_back (ch.data());
    }

    std::vector<std::vector<float>> data;
    std::vector<const float*> pointers;
};

} // namespace

TEST_CASE ("Regular callbacks produce no late count", "[stats]")
{
    CallbackStats stats;
    stats.reset (48000.0, 128, 2);

    Block block (2, 128);
    const auto period = 128.0 / 48000.0;

    for (int i = 0; i < 100; ++i)
        stats.process (10.0 + i * period, block.pointers.data(), 2, 128);

    const auto s = stats.snapshot();
    REQUIRE (s.callbacks == 100);
    REQUIRE (s.samples == 12800);
    REQUIRE (s.lateCallbacks == 0);
    REQUIRE (s.meanIntervalMs == Approx (period * 1000.0).epsilon (1e-6));
    REQUIRE (s.maxIntervalMs == Approx (period * 1000.0).epsilon (1e-6));
    REQUIRE (s.minBlockSize == 128);
    REQUIRE (s.maxBlockSize == 128);
}

TEST_CASE ("A gap longer than 1.5 periods counts as late", "[stats]")
{
    CallbackStats stats;
    stats.reset (48000.0, 256, 1);

    Block block (1, 256);
    const auto period = 256.0 / 48000.0;

    auto t = 0.0;
    stats.process (t, block.pointers.data(), 1, 256);
    stats.process (t += period, block.pointers.data(), 1, 256);
    stats.process (t += period * 3.0, block.pointers.data(), 1, 256); // stall
    stats.process (t += period, block.pointers.data(), 1, 256);

    const auto s = stats.snapshot();
    REQUIRE (s.lateCallbacks == 1);
    REQUIRE (s.maxIntervalMs == Approx (period * 3000.0));
}

TEST_CASE ("Peaks, peak hold and clips are tracked per channel", "[stats]")
{
    CallbackStats stats;
    stats.reset (48000.0, 64, 3);

    Block block (3, 64);
    block.data[0][10] = 0.5f;
    block.data[1][20] = -0.25f;
    block.data[2][30] = 1.0f;

    stats.process (0.0, block.pointers.data(), 3, 64);

    REQUIRE (stats.takeInputPeak (0) == 0.5f);
    REQUIRE (stats.takeInputPeak (1) == 0.25f);
    REQUIRE (stats.takeInputPeak (2) == 1.0f);

    // takeInputPeak resets the live peak but not the hold.
    REQUIRE (stats.takeInputPeak (0) == 0.0f);
    REQUIRE (stats.getInputPeakHold (0) == 0.5f);

    REQUIRE (stats.getClipCount (0) == 0);
    REQUIRE (stats.getClipCount (2) == 1);

    // Out-of-range channels are harmless.
    REQUIRE (stats.takeInputPeak (-1) == 0.0f);
    REQUIRE (stats.getInputPeakHold (CallbackStats::maxChannels) == 0.0f);
}

TEST_CASE ("More device channels than configured are ignored safely", "[stats]")
{
    CallbackStats stats;
    stats.reset (48000.0, 32, 1);

    Block block (4, 32, 0.5f);
    stats.process (0.0, block.pointers.data(), 4, 32);

    REQUIRE (stats.getInputPeakHold (0) == 0.5f);
    REQUIRE (stats.getInputPeakHold (1) == 0.0f);
}

TEST_CASE ("reset clears everything", "[stats]")
{
    CallbackStats stats;
    stats.reset (48000.0, 32, 1);

    Block block (1, 32, 0.9f);
    stats.process (0.0, block.pointers.data(), 1, 32);
    stats.process (1.0, block.pointers.data(), 1, 32);

    stats.reset (44100.0, 64, 1);

    const auto s = stats.snapshot();
    REQUIRE (s.callbacks == 0);
    REQUIRE (s.lateCallbacks == 0);
    REQUIRE (stats.getInputPeakHold (0) == 0.0f);
}
