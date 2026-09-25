// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "nodes/NodeTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace spm::graph
{

using NodeId = std::uint64_t;
using WireId = std::uint64_t;

/** Plain description of a mixer graph: what the session model hands to the builder. */
struct NodeDesc
{
    NodeId id = 0;
    std::string type;
    nodes::ParamValues params;
};

struct WireDesc
{
    WireId id = 0;
    NodeId sourceNode = 0;
    int sourcePort = 0;
    NodeId destNode = 0;
    int destPort = 0;
    float gain = 1.0f;  // linear
};

struct GraphDesc
{
    std::vector<NodeDesc> nodes;
    std::vector<WireDesc> wires;

    const NodeDesc* findNode (NodeId id) const noexcept
    {
        for (const auto& n : nodes)
            if (n.id == id)
                return &n;

        return nullptr;
    }
};

} // namespace spm::graph
