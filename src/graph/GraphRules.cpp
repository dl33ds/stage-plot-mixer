// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "graph/GraphRules.h"

#include <unordered_set>

namespace spm::graph
{

std::optional<nodes::PortLayout> layoutOf (const NodeDesc& node, const nodes::NodeRegistry& registry)
{
    const auto* type = registry.find (node.type);

    if (type == nullptr)
        return std::nullopt;

    return type->layout (type->sanitise (node.params));
}

bool reaches (const GraphDesc& graph, NodeId from, NodeId to)
{
    std::vector<NodeId> stack { from };
    std::unordered_set<NodeId> seen { from };

    while (! stack.empty())
    {
        const auto current = stack.back();
        stack.pop_back();

        if (current == to)
            return true;

        for (const auto& w : graph.wires)
            if (w.sourceNode == current && seen.insert (w.destNode).second)
                stack.push_back (w.destNode);
    }

    return false;
}

std::string checkConnection (const GraphDesc& graph, NodeId sourceNode, int sourcePort, NodeId destNode, int destPort,
                             const nodes::NodeRegistry& registry)
{
    if (sourceNode == destNode)
        return "A node can't be wired to itself";

    const auto* src = graph.findNode (sourceNode);
    const auto* dst = graph.findNode (destNode);

    if (src == nullptr || dst == nullptr)
        return "That node no longer exists";

    const auto srcLayout = layoutOf (*src, registry);
    const auto dstLayout = layoutOf (*dst, registry);

    if (! srcLayout || ! dstLayout
        || sourcePort < 0 || sourcePort >= (int) srcLayout->outputs.size()
        || destPort < 0 || destPort >= (int) dstLayout->inputs.size())
        return "That port doesn't exist";

    for (const auto& w : graph.wires)
        if (w.sourceNode == sourceNode && w.sourcePort == sourcePort && w.destNode == destNode && w.destPort == destPort)
            return "These ports are already connected";

    if (reaches (graph, destNode, sourceNode))
        return "This would create a feedback loop";

    return {};
}

} // namespace spm::graph
