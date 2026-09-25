// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EngineRig.h"

#include "graph/GraphRules.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace spm;
using namespace spm::test;
using Catch::Approx;
namespace types = nodes::types;

namespace
{

struct LiveRig : EngineRig
{
    LiveRig (int inputs, int outputs) : EngineRig (inputs, outputs)
    {
        core.setOutputsMuted (false);
        input = [] (int ch, std::int64_t i) { return (float) std::sin ((double) i * 0.02 + ch) * 0.5f; };
    }
};

} // namespace

TEST_CASE ("Adding and removing a wire fades instead of clicking", "[graph]")
{
    LiveRig rig (1, 1);

    graph::GraphDesc d;
    d.nodes = { node (1, types::hardwareInput), node (2, types::hardwareOutput, { 1, 1 }) };
    rig.submit (d);
    rig.run (4800);  // output fade-in done, nothing wired yet

    rig.clearRecording();
    d.wires = { wire (10, 1, 0, 2, 0) };
    rig.submit (d);
    rig.run (4800);

    // Sine step is at most 0.5 × 0.02 = 0.01 per sample; a click would be far bigger.
    REQUIRE (largestStep (rig.recorded[0]) < 0.012f);
    REQUIRE (rig.recorded[0].back() == rig.input (0, rig.position - 1));

    rig.clearRecording();
    d.wires.clear();
    rig.submit (d);
    rig.run (4800);
    REQUIRE (largestStep (rig.recorded[0]) < 0.012f);
    REQUIRE (rig.recorded[0].back() == 0.0f);

    REQUIRE (rig.builder.hasFades());
    REQUIRE (rig.builder.pruneFinishedFades (true));
    REQUIRE (! rig.builder.hasFades());
}

TEST_CASE ("Removing a node keeps it running until its wires fade", "[graph]")
{
    LiveRig rig (1, 1);

    graph::GraphDesc d;
    d.nodes = { node (1, types::hardwareInput), node (2, types::gain, { 0, 0, 0, 1 }), node (3, types::hardwareOutput, { 1, 1 }) };
    d.wires = { wire (10, 1, 0, 2, 0), wire (11, 2, 0, 3, 0) };
    rig.submit (d);
    rig.settle();

    rig.clearRecording();
    d.nodes.erase (d.nodes.begin() + 1);
    d.wires.clear();
    rig.submit (d);
    REQUIRE (rig.builder.getNumRetiredNodes() == 1);
    rig.run (4800);

    REQUIRE (largestStep (rig.recorded[0]) < 0.012f);
    REQUIRE (rig.builder.pruneFinishedFades (true));
    REQUIRE (rig.builder.getNumRetiredNodes() == 0);
}

TEST_CASE ("Changing a node's channel count swaps its processor without a click", "[graph]")
{
    LiveRig rig (2, 2);

    graph::GraphDesc d;
    d.nodes = { node (1, types::hardwareInput, { 1, 1 }), node (2, types::gain, { 0, 0, 0, 1 }), node (3, types::hardwareOutput, { 1, 1 }) };
    d.wires = { wire (10, 1, 0, 2, 0), wire (11, 2, 0, 3, 0) };
    rig.submit (d);
    rig.settle();

    auto* before = rig.builder.getProcessor (2);
    rig.clearRecording();
    d.nodes[1].params[3] = 2;
    rig.submit (d);
    rig.run (4800);

    REQUIRE (rig.builder.getProcessor (2) != before);
    REQUIRE (rig.builder.getProcessor (2)->getInputChannels (0) == 2);
    REQUIRE (largestStep (rig.recorded[0]) < 0.012f);

    // A non-structural change keeps the processor.
    auto* after = rig.builder.getProcessor (2);
    d.nodes[1].params[0] = -6.0f;
    rig.submit (d);
    REQUIRE (rig.builder.getProcessor (2) == after);
}

TEST_CASE ("Channel mapping between ports of different widths", "[graph]")
{
    LiveRig rig (2, 4);
    rig.input = [] (int ch, std::int64_t) { return ch == 0 ? 0.25f : 0.75f; };

    graph::GraphDesc d;
    d.nodes = { node (1, types::hardwareInput, { 1, 1 }),   // mono
                node (2, types::hardwareInput, { 1, 2 }),   // stereo
                node (3, types::hardwareOutput, { 1, 2 }),  // outs 1-2 from mono
                node (4, types::hardwareOutput, { 3, 1 }) };// out 3 from stereo
    d.wires = { wire (10, 1, 0, 3, 0), wire (11, 2, 0, 4, 0) };
    rig.submit (d);
    rig.settle();

    REQUIRE (rig.recorded[0].back() == 0.25f);  // mono duplicated
    REQUIRE (rig.recorded[1].back() == 0.25f);
    REQUIRE (rig.recorded[2].back() == Approx (0.5f));  // stereo averaged into mono
    REQUIRE (rig.recorded[3].back() == 0.0f);
}

TEST_CASE ("Wires into one input are summed, with wire gain", "[graph]")
{
    LiveRig rig (2, 1);
    rig.input = [] (int ch, std::int64_t) { return ch == 0 ? 0.25f : 0.5f; };

    graph::GraphDesc d;
    d.nodes = { node (1, types::hardwareInput, { 1, 1 }), node (2, types::hardwareInput, { 2, 1 }),
                node (3, types::bus, { 0, 0, 1 }), node (4, types::hardwareOutput, { 1, 1 }) };
    d.wires = { wire (10, 1, 0, 3, 0), wire (11, 2, 0, 3, 0, 0.5f), wire (12, 3, 0, 4, 0) };
    rig.submit (d);
    rig.settle();
    REQUIRE (rig.recorded[0].back() == Approx (0.5f));

    REQUIRE (rig.builder.setWireGain (11, 0.0f));
    rig.settle();
    REQUIRE (rig.recorded[0].back() == Approx (0.25f));
}

TEST_CASE ("Loops, duplicates and bad ports are refused", "[graph]")
{
    graph::GraphDesc d;
    d.nodes = { node (1, types::gain), node (2, types::gain), node (3, types::hardwareOutput) };
    d.wires = { wire (10, 1, 0, 2, 0) };

    REQUIRE (graph::checkConnection (d, 2, 0, 3, 0).empty());
    REQUIRE (graph::checkConnection (d, 2, 0, 1, 0) == "This would create a feedback loop");
    REQUIRE (graph::checkConnection (d, 1, 0, 1, 0) == "A node can't be wired to itself");
    REQUIRE (graph::checkConnection (d, 1, 0, 2, 0) == "These ports are already connected");
    REQUIRE (graph::checkConnection (d, 1, 1, 2, 0) == "That port doesn't exist");
    REQUIRE (graph::checkConnection (d, 3, 0, 1, 0) == "That port doesn't exist");  // outputs have no out ports

    // The builder drops a looping wire rather than failing.
    d.wires.push_back (wire (11, 2, 0, 1, 0));
    graph::GraphBuilder builder;
    auto compiled = builder.build (d);
    REQUIRE (compiled->getNumNodes() == 3);
    REQUIRE (builder.isWireActive (10));
    REQUIRE (! builder.isWireActive (11));
}

TEST_CASE ("Reversing a wire while the old one fades doesn't form a loop", "[graph]")
{
    LiveRig rig (1, 1);

    graph::GraphDesc d;
    d.nodes = { node (1, types::gain), node (2, types::gain) };
    d.wires = { wire (10, 1, 0, 2, 0) };
    rig.submit (d);
    rig.run (256);

    d.wires = { wire (11, 2, 0, 1, 0) };
    auto compiled = rig.builder.build (d);
    REQUIRE (compiled->getNumNodes() == 2);
    REQUIRE (rig.builder.isWireActive (11));
}
