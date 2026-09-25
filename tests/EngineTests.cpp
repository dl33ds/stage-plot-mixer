// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "EngineRig.h"

#include "engine/GraphHandoff.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace spm;
using namespace spm::test;
using Catch::Approx;
namespace types = nodes::types;

namespace
{

graph::GraphDesc passthrough (int channels)
{
    graph::GraphDesc d;
    d.nodes.push_back (node (1, types::hardwareInput, { 1, (float) channels }));
    d.nodes.push_back (node (2, types::hardwareOutput, { 1, (float) channels }));
    d.wires.push_back (wire (10, 1, 0, 2, 0));
    return d;
}

} // namespace

TEST_CASE ("Outputs stay silent until unmuted, then fade in", "[engine]")
{
    EngineRig rig (2, 2);
    rig.submit (passthrough (2));
    rig.run (4800);

    for (auto& ch : rig.recorded)
        for (auto v : ch)
            REQUIRE (v == 0.0f);

    rig.clearRecording();
    rig.core.setOutputsMuted (false);
    rig.run (4800);

    // 50 ms fade = 2400 samples; afterwards output equals input exactly.
    const auto& out = rig.recorded[0];
    REQUIRE (std::abs (out[10]) < 0.01f);

    for (size_t i = 2400; i < out.size(); ++i)
        REQUIRE (out[i] == rig.input (0, 4800 + (std::int64_t) i));
}

TEST_CASE ("Pass-through is bit-exact at any device block size", "[engine]")
{
    for (int block : { 16, 64, 128, 1000, 2048, 4096 })
    {
        EngineRig rig (4, 4, 48000.0, block);
        rig.core.setOutputsMuted (false);
        rig.submit (passthrough (4));
        rig.run (48000);

        for (int ch = 0; ch < 4; ++ch)
            for (size_t i = 4800; i < rig.recorded[(size_t) ch].size(); ++i)
                REQUIRE (rig.recorded[(size_t) ch][i] == rig.input (ch, (std::int64_t) i));
    }
}

TEST_CASE ("Graph handoff retires old graphs for the message thread to delete", "[engine]")
{
    engine::GraphHandoff handoff;
    REQUIRE (handoff.acquire() == nullptr);

    handoff.submit (std::make_unique<engine::CompiledGraph>());
    auto* first = handoff.acquire();
    REQUIRE (first != nullptr);

    // Two submits before the audio thread looks: only the latest is used.
    handoff.submit (std::make_unique<engine::CompiledGraph>());
    handoff.submit (std::make_unique<engine::CompiledGraph>());
    auto* second = handoff.acquire();
    REQUIRE (second != nullptr);
    REQUIRE (second != first);
    REQUIRE (handoff.acquire() == second);

    // Many swaps without garbage collection: acquire keeps working and never drops a graph.
    for (int i = 0; i < 200; ++i)
    {
        handoff.submit (std::make_unique<engine::CompiledGraph>());
        REQUIRE (handoff.acquire() != nullptr);
        if (i % 50 == 0)
            handoff.collectGarbage();
    }
}

TEST_CASE ("Late callbacks and CPU load are reported", "[engine]")
{
    EngineRig rig (1, 1, 48000.0, 128);
    rig.submit (passthrough (1));
    rig.run (128 * 10);

    auto stats = rig.core.takeStats();
    REQUIRE (stats.callbacks == 10);
    REQUIRE (stats.lateCallbacks == 0);

    rig.clock += 0.05;  // a 50 ms stall
    rig.run (128);
    stats = rig.core.takeStats();
    REQUIRE (stats.lateCallbacks == 1);
    REQUIRE (stats.blockSize == 128);
}
