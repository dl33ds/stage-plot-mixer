// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "graph/GraphBuilder.h"

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace spm::ui
{

/** Levels for one port, read once per UI frame. Reading a meter resets its peak, so every
    view (node meters, wire glow, inspector) reads from here instead of the engine.
    Levels are in dBFS with ballistics applied (fast attack, 24 dB/s release).
*/
struct PortLevels
{
    std::vector<float> peakDb, rmsDb;
    bool clipped = false;
    float maxPeakDb() const noexcept;
};

class MeterCache
{
public:
    /** Polls every port of the given nodes. */
    void poll (graph::GraphBuilder& builder, const juce::Array<graph::NodeId>& nodes, double elapsedSeconds);

    const PortLevels* get (graph::NodeId node, bool input, int port) const;

    /** Clears the clip indicators of a node's meters. */
    void clearClips (graph::GraphBuilder& builder, graph::NodeId node);

    static constexpr float floorDb = -60.0f;

private:
    std::map<std::tuple<graph::NodeId, bool, int>, PortLevels> levels;
};

} // namespace spm::ui
