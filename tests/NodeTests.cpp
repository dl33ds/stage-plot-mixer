// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EngineRig.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace spm;
using namespace spm::test;
using Catch::Approx;
namespace types = nodes::types;

namespace
{

/** Runs `middle` between a constant-valued input and the outputs, returning the settled output. */
std::vector<float> settledOutput (int inputs, int outputs, std::vector<graph::NodeDesc> middle,
                                  std::vector<graph::WireDesc> wires, std::function<float (int)> value)
{
    EngineRig rig (inputs, outputs);
    rig.core.setOutputsMuted (false);
    rig.input = [value] (int ch, std::int64_t) { return value (ch); };

    graph::GraphDesc d;
    d.nodes.push_back (node (1, types::hardwareInput, { 1, (float) inputs }));
    d.nodes.push_back (node (2, types::hardwareOutput, { 1, (float) outputs }));
    d.nodes.insert (d.nodes.end(), middle.begin(), middle.end());
    d.wires = std::move (wires);
    rig.submit (d);
    rig.run (9600);

    std::vector<float> result;
    for (auto& ch : rig.recorded)
        result.push_back (ch.back());
    return result;
}

} // namespace

TEST_CASE ("Every built-in type has consistent parameters and creates a processor", "[nodes]")
{
    for (const auto& type : nodes::NodeRegistry::builtIn().all())
    {
        INFO (type.id);
        const auto values = type.defaults();
        REQUIRE (values.size() == type.params.size());
        REQUIRE (type.sanitise (values) == values);

        const auto layout = type.layout (values);
        if (type.id == types::group)
            continue;  // flattened away: groups never reach the engine

        auto processor = type.create (values, layout);
        REQUIRE (processor->getNumInputPorts() == (int) layout.inputs.size());
        REQUIRE (processor->getNumOutputPorts() == (int) layout.outputs.size());
    }

    const auto* gain = nodes::NodeRegistry::builtIn().find (types::gain);
    REQUIRE (gain->sanitise ({ 100.0f, 0.4f }) == nodes::ParamValues { 24.0f, 0.0f, 0.0f, 1.0f });
}

TEST_CASE ("Gain node: level, polarity and mute", "[nodes]")
{
    auto one = [] (int) { return 0.5f; };
    std::vector<graph::WireDesc> w { wire (10, 1, 0, 3, 0), wire (11, 3, 0, 2, 0) };

    REQUIRE (settledOutput (1, 1, { node (3, types::gain, { -6.0206f }) }, w, one)[0] == Approx (0.25f).margin (1e-4));
    REQUIRE (settledOutput (1, 1, { node (3, types::gain, { 0, 1 }) }, w, one)[0] == Approx (-0.5f));
    REQUIRE (settledOutput (1, 1, { node (3, types::gain, { 0, 0, 1 }) }, w, one)[0] == 0.0f);
    REQUIRE (settledOutput (1, 1, { node (3, types::fader, { -100.0f }) }, w, one)[0] == 0.0f);
}

TEST_CASE ("Pan node: constant power for mono, balance for stereo", "[nodes]")
{
    auto one = [] (int) { return 1.0f; };
    std::vector<graph::WireDesc> w { wire (10, 1, 0, 3, 0), wire (11, 3, 0, 2, 0) };

    auto centre = settledOutput (1, 2, { node (3, types::pan, { 0.0f, 1 }) }, w, one);
    REQUIRE (centre[0] == Approx (0.7071f).margin (1e-3));
    REQUIRE (centre[1] == Approx (0.7071f).margin (1e-3));

    auto left = settledOutput (1, 2, { node (3, types::pan, { -1.0f, 1 }) }, w, one);
    REQUIRE (left[0] == Approx (1.0f));
    REQUIRE (left[1] == Approx (0.0f).margin (1e-6));

    auto stereoCentre = settledOutput (2, 2, { node (3, types::pan, { 0.0f, 2 }) }, w, one);
    REQUIRE (stereoCentre[0] == Approx (1.0f));
    REQUIRE (stereoCentre[1] == Approx (1.0f));

    auto stereoRight = settledOutput (2, 2, { node (3, types::pan, { 1.0f, 2 }) }, w, one);
    REQUIRE (stereoRight[0] == Approx (0.0f).margin (1e-6));
    REQUIRE (stereoRight[1] == Approx (1.0f));
}

TEST_CASE ("Channel pick, bundle and unbundle move the right channels", "[nodes]")
{
    auto byChannel = [] (int ch) { return (float) (ch + 1) * 0.1f; };

    // Take channels 3-4 of 4 into outputs 1-2.
    auto picked = settledOutput (4, 2, { node (3, types::channelPick, { 4, 3, 2 }) },
                                 { wire (10, 1, 0, 3, 0), wire (11, 3, 0, 2, 0) }, byChannel);
    REQUIRE (picked[0] == Approx (0.3f));
    REQUIRE (picked[1] == Approx (0.4f));

    // Unbundle 3 channels, swap 1 and 3, bundle again.
    auto swapped = settledOutput (3, 3, { node (3, types::unbundle, { 3 }), node (4, types::bundle, { 3 }) },
                                  { wire (10, 1, 0, 3, 0), wire (11, 3, 0, 4, 2), wire (12, 3, 1, 4, 1),
                                    wire (13, 3, 2, 4, 0), wire (14, 4, 0, 2, 0) }, byChannel);
    REQUIRE (swapped[0] == Approx (0.3f));
    REQUIRE (swapped[1] == Approx (0.2f));
    REQUIRE (swapped[2] == Approx (0.1f));
}

TEST_CASE ("Test generator produces a sine at the requested level", "[nodes]")
{
    EngineRig rig (0, 1);
    rig.core.setOutputsMuted (false);

    graph::GraphDesc d;
    d.nodes = { node (1, types::testGenerator, { 0, 1000, -6.0206f, 1, 1 }), node (2, types::hardwareOutput, { 1, 1 }) };
    d.wires = { wire (10, 1, 0, 2, 0) };
    rig.submit (d);
    rig.run (48000);

    float peak = 0.0f;
    for (size_t i = 24000; i < rig.recorded[0].size(); ++i)
        peak = std::max (peak, std::abs (rig.recorded[0][i]));
    REQUIRE (peak == Approx (0.5f).margin (1e-3));

    // Meters on the output port see it too.
    auto& meter = rig.builder.getProcessor (1)->getOutputMeter (0);
    REQUIRE (meter.takeMaxPeak() == Approx (0.5f).margin (1e-3));
}
