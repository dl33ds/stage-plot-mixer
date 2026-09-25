// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "graph/GraphDesc.h"

#include <optional>
#include <string>

namespace spm::graph
{

/** The port layout of a node, or nothing if its type is unknown. */
std::optional<nodes::PortLayout> layoutOf (const NodeDesc& node, const nodes::NodeRegistry& registry = nodes::NodeRegistry::builtIn());

/** True if signal can flow from `from` to `to` along the graph's wires (or from == to). */
bool reaches (const GraphDesc& graph, NodeId from, NodeId to);

/** Checks whether a new wire would be allowed. Returns an empty string if so, otherwise
    a short reason the UI can show, e.g. "This would create a feedback loop".
*/
std::string checkConnection (const GraphDesc& graph, NodeId sourceNode, int sourcePort, NodeId destNode, int destPort,
                             const nodes::NodeRegistry& registry = nodes::NodeRegistry::builtIn());

} // namespace spm::graph
