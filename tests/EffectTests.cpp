// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EngineRig.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

using namespace spm;
using namespace spm::test;
using Catch::Approx;
namespace types = nodes::types;

namespace
{

constexpr double rate = 48000.0;

/** Input 1 → effect (node 3) → output 1, run for numSamples; returns every output channel. */
std::vector<std::vector<float>> runThrough (graph::NodeDesc effect, int inputs, int outputs,
                                            std::function<float (int, std::int64_t)> input, int numSamples)
{
    EngineRig rig (inputs, outputs, rate);
    rig.core.setOutputsMuted (false);
    rig.input = std::move (input);

    graph::GraphDesc d;
    d.nodes.push_back (node (1, types::hardwareInput, { 1, (float) inputs }));
    d.nodes.push_back (node (2, types::hardwareOutput, { 1, (float) outputs }));
    d.nodes.push_back (std::move (effect));
    d.wires = { wire (10, 1, 0, 3, 0), wire (11, 3, 0, 2, 0) };
    rig.submit (d);
    rig.run (numSamples);
    return rig.recorded;
}

std::function<float (int, std::int64_t)> sine (double hz, float amplitude)
{
    return [hz, amplitude] (int, std::int64_t i)
    {
        return amplitude * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / rate);
    };
}

float peakOf (const std::vector<float>& x, size_t from)
{
    float peak = 0.0f;
    for (auto i = from; i < x.size(); ++i)
        peak = std::max (peak, std::abs (x[i]));
    return peak;
}

} // namespace

TEST_CASE ("EQ at 0 dB is transparent", "[effects]")
{
    const auto out = runThrough (node (3, types::eq), 1, 1, sine (440.0, 0.5f), 9600);

    // After the input wire's fade-in, the output is the input.
    const auto in = sine (440.0, 0.5f);
    float error = 0.0f;
    for (size_t i = 4800; i < out[0].size(); ++i)
        error = std::max (error, std::abs (out[0][i] - in (0, (std::int64_t) i)));
    REQUIRE (error < 1.0e-4f);
}

TEST_CASE ("EQ boosts and cuts its bands", "[effects]")
{
    // Low mid +12 dB at 400 Hz; a 400 Hz sine comes out 4 times as loud.
    const auto boosted = runThrough (node (3, types::eq, { 0, 100, 12.0f, 400.0f, 1.0f }), 1, 1, sine (400.0, 0.1f), 19200);
    REQUIRE (peakOf (boosted[0], 14400) == Approx (0.398f).margin (0.01));

    // High shelf -12 dB: 16 kHz is cut, 100 Hz untouched.
    const auto shelf = node (3, types::eq, { 0, 100, 0, 400, 1, 0, 2500, 1, -12.0f, 8000.0f });
    REQUIRE (peakOf (runThrough (shelf, 1, 1, sine (16000.0, 0.5f), 9600)[0], 4800) < 0.15f);
    REQUIRE (peakOf (runThrough (shelf, 1, 1, sine (100.0, 0.5f), 19200)[0], 9600) == Approx (0.5f).margin (0.01));
}

TEST_CASE ("Filter: the high-pass cuts rumble and passes the voice", "[effects]")
{
    // Default: high-pass on at 80 Hz, 12 dB/oct.
    REQUIRE (peakOf (runThrough (node (3, types::filter), 1, 1, sine (20.0, 0.5f), 48000)[0], 24000) < 0.05f);
    REQUIRE (peakOf (runThrough (node (3, types::filter), 1, 1, sine (1000.0, 0.5f), 9600)[0], 4800) == Approx (0.5f).margin (0.01));

    // 24 dB/oct cuts more at the same frequency.
    const auto steep = node (3, types::filter, { 1, 80, 0, 12000, 1 });
    REQUIRE (peakOf (runThrough (steep, 1, 1, sine (20.0, 0.5f), 48000)[0], 24000) < 0.005f);

    // Low-pass on at 1 kHz: 8 kHz is cut.
    const auto lowPass = node (3, types::filter, { 0, 80, 1, 1000 });
    REQUIRE (peakOf (runThrough (lowPass, 1, 1, sine (8000.0, 0.5f), 9600)[0], 4800) < 0.02f);
}

TEST_CASE ("Compressor: steady-state gain reduction follows threshold and ratio", "[effects]")
{
    // 0.5 (-6 dBFS), threshold -20, 4:1, hard knee: 14 dB over becomes 3.5 dB over,
    // so 10.5 dB of reduction and -16.5 dBFS out.
    const auto comp = node (3, types::compressor, { -20.0f, 4.0f, 1.0f, 50.0f, 0.0f, 0.0f });
    const auto out = runThrough (comp, 1, 1, [] (int, std::int64_t) { return 0.5f; }, 19200);
    REQUIRE (out[0].back() == Approx (std::pow (10.0f, -16.5f / 20.0f)).margin (1e-3));

    // Below the threshold it does nothing; makeup adds gain.
    const auto quiet = runThrough (node (3, types::compressor, { -20.0f, 4.0f, 1.0f, 50.0f, 0.0f, 6.0f }), 1, 1,
                                   [] (int, std::int64_t) { return 0.01f; }, 9600);
    REQUIRE (quiet[0].back() == Approx (0.01995f).margin (1e-4));
}

TEST_CASE ("Limiter: never goes over the ceiling, and reports its latency", "[effects]")
{
    // Loud and spiky: a sine pushed 12 dB, with single-sample spikes.
    auto spiky = [] (int ch, std::int64_t i)
    {
        const auto s = 0.9f * (float) std::sin ((double) i * 0.03 * (ch + 1));
        return i % 997 == 0 ? 1.0f : s;
    };

    const auto ceiling = std::pow (10.0f, -1.0f / 20.0f);
    const auto out = runThrough (node (3, types::limiter, { 12.0f, -1.0f, 50.0f, 0.0f, 2.0f }), 2, 2, spiky, 48000);

    for (const auto& ch : out)
        REQUIRE (peakOf (ch, 0) <= ceiling + 1.0e-6f);

    // Still loud: it limits rather than muting.
    REQUIRE (peakOf (out[0], 24000) > 0.8f);

    EngineRig rig (1, 1);
    graph::GraphDesc d;
    d.nodes = { node (3, types::limiter, { 0, -1, 50, 0, 1 }) };
    rig.submit (d);
    REQUIRE (rig.builder.getProcessor (3)->getLatencySamples() == 64);
}

TEST_CASE ("Gate: closes on quiet input and opens on loud input", "[effects]")
{
    const auto gate = node (3, types::gate, { -50.0f, -80.0f });
    REQUIRE (runThrough (gate, 1, 1, [] (int, std::int64_t) { return 0.001f; }, 48000)[0].back() == 0.0f);
    REQUIRE (runThrough (gate, 1, 1, [] (int, std::int64_t) { return 0.1f; }, 9600)[0].back() == Approx (0.1f));

    // With a range of -20 dB it only turns down by that much.
    const auto partial = node (3, types::gate, { -50.0f, -20.0f });
    REQUIRE (runThrough (partial, 1, 1, [] (int, std::int64_t) { return 0.001f; }, 48000)[0].back() == Approx (0.0001f).margin (1e-6));
}

TEST_CASE ("Delay: the echo arrives after the delay time, and repeats with feedback", "[effects]")
{
    constexpr std::int64_t at = 6000;
    auto impulse = [] (int, std::int64_t i) { return i == at ? 1.0f : 0.0f; };

    // 10 ms = 480 samples, fully wet, 50% feedback.
    const auto out = runThrough (node (3, types::delay, { 10.0f, 50.0f, 100.0f }), 1, 1, impulse, 9600)[0];

    REQUIRE (peakOf (std::vector<float> (out.begin(), out.begin() + at + 470), 0) < 1.0e-6f);  // no dry signal
    REQUIRE (out[(size_t) (at + 480)] == Approx (1.0f).margin (0.01));
    REQUIRE (out[(size_t) (at + 960)] == Approx (0.5f).margin (0.01));
    REQUIRE (out[(size_t) (at + 1440)] == Approx (0.25f).margin (0.01));
}

TEST_CASE ("Reverb: stereo tail that decays and stays finite", "[effects]")
{
    auto burst = [] (int ch, std::int64_t i)
    {
        return i >= 4800 && i < 5280 ? (float) std::sin ((double) i * 0.37 * (ch + 1)) * 0.5f : 0.0f;
    };

    const auto out = runThrough (node (3, types::reverb, { 80.0f, 50.0f, 100.0f, 100.0f, 1.0f }), 1, 2, burst, 4 * 48000);

    for (const auto& ch : out)
        for (auto x : ch)
            REQUIRE (std::isfinite (x));

    // A tail after the burst on both sides, quieter a few seconds later.
    const auto early = peakOf (std::vector<float> (out[0].begin() + 9600, out[0].begin() + 24000), 0);
    const auto late = peakOf (std::vector<float> (out[0].end() - 4800, out[0].end()), 0);
    REQUIRE (early > 0.01f);
    REQUIRE (peakOf (std::vector<float> (out[1].begin() + 9600, out[1].begin() + 24000), 0) > 0.01f);
    REQUIRE (late < early * 0.5f);
}

TEST_CASE ("PDC: a path through a limiter and a direct path sum in time", "[effects][pdc]")
{
    EngineRig rig (1, 1, rate);
    rig.core.setOutputsMuted (false);
    rig.input = sine (300.0, 0.1f);  // quiet: the limiter leaves it alone

    graph::GraphDesc d;
    d.nodes = { node (1, types::hardwareInput, { 1, 1 }), node (2, types::hardwareOutput, { 1, 1 }),
                node (3, types::limiter, { 0, -1, 50, 0, 1 }) };
    d.wires = { wire (10, 1, 0, 3, 0), wire (11, 3, 0, 2, 0), wire (12, 1, 0, 2, 0) };
    rig.submit (d);

    REQUIRE (rig.builder.getWireDelay (12) == 64);
    REQUIRE (rig.builder.getWireDelay (10) == 0);
    REQUIRE (rig.builder.getWireDelay (11) == 0);
    REQUIRE (rig.builder.getPathLatency (2) == 64);

    rig.run (9600);

    // Both paths line up: twice the input, 64 samples late (no comb filtering).
    const auto in = sine (300.0, 0.1f);
    float error = 0.0f;
    for (size_t i = 4800; i < rig.recorded[0].size(); ++i)
        error = std::max (error, std::abs (rig.recorded[0][i] - 2.0f * in (0, (std::int64_t) i - 64)));
    REQUIRE (error < 1.0e-4f);

    // Removing the limiter path removes the delay; the change fades rather than clicks.
    d.nodes.pop_back();
    d.wires = { wire (12, 1, 0, 2, 0) };
    rig.clearRecording();
    rig.submit (d);
    REQUIRE (rig.builder.getWireDelay (12) == 0);
    rig.run (9600);
    REQUIRE (largestStep (rig.recorded[0]) < 0.02f);
}

TEST_CASE ("PDC: recorders are lined up with each other", "[effects][pdc]")
{
    EngineRig rig (1, 1, rate);

    graph::GraphDesc d;
    d.nodes = { node (1, types::hardwareInput, { 1, 1 }), node (3, types::limiter, { 0, -1, 50, 0, 1 }),
                node (4, types::recorder, { 1, 0, 0, 1 }), node (5, types::recorder, { 1, 0, 0, 1 }) };
    d.wires = { wire (10, 1, 0, 3, 0), wire (11, 3, 0, 4, 0), wire (12, 1, 0, 5, 0) };
    rig.submit (d);

    // The direct recording is delayed to match the one through the limiter.
    REQUIRE (rig.builder.getWireDelay (11) == 0);
    REQUIRE (rig.builder.getWireDelay (12) == 64);
    REQUIRE (rig.builder.getPathLatency (4) == 64);
    REQUIRE (rig.builder.getPathLatency (5) == 64);
}
