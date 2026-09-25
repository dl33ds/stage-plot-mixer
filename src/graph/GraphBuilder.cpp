// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "graph/GraphBuilder.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace spm::graph
{

GraphBuilder::GraphBuilder (const nodes::NodeRegistry& r, int maxBlockSize)
    : registry (r), maxBlock (maxBlockSize)
{
}

GraphBuilder::Key GraphBuilder::createNode (const nodes::NodeType& type, const nodes::ParamValues& params)
{
    NodeEntry entry;
    entry.type = &type;
    entry.params = params;
    entry.layout = type.layout (params);
    entry.processor = type.create (params, entry.layout);

    for (size_t i = 0; i < params.size(); ++i)
        entry.processor->setParameter ((int) i, params[i]);

    const auto key = nextKey++;
    entries.emplace (key, std::move (entry));
    return key;
}

void GraphBuilder::retireWire (WireEntry wire)
{
    wire.state->setConnected (false);
    fadingWires.push_back (std::move (wire));
}

int GraphBuilder::getNumRetiredNodes() const noexcept
{
    return (int) std::count_if (entries.begin(), entries.end(), [] (const auto& e) { return ! e.second.live; });
}

void GraphBuilder::dropUnusedRetiredNodes()
{
    std::set<Key> used;
    for (const auto& w : fadingWires)
    {
        used.insert (w.source);
        used.insert (w.dest);
    }

    for (auto it = entries.begin(); it != entries.end();)
    {
        if (! it->second.live && used.count (it->first) == 0)
            it = entries.erase (it);
        else
            ++it;
    }
}

std::unique_ptr<engine::CompiledGraph> GraphBuilder::build (const GraphDesc& desc)
{
    // Nodes ----------------------------------------------------------------------------
    std::set<NodeId> seenNodes;

    for (const auto& n : desc.nodes)
    {
        const auto* type = registry.find (n.type);

        if (type == nullptr || ! seenNodes.insert (n.id).second)
            continue;

        const auto params = type->sanitise (n.params);
        const auto existing = liveNodes.find (n.id);

        if (existing != liveNodes.end())
        {
            auto& entry = entries.at (existing->second);

            if (entry.type == type && type->sameStructure (entry.params, params))
            {
                for (size_t i = 0; i < params.size(); ++i)
                    entry.processor->setParameter ((int) i, params[i]);

                entry.params = params;
                continue;
            }

            entry.live = false;  // replaced: keeps running while its wires fade out
        }

        liveNodes[n.id] = createNode (*type, params);
    }

    for (auto it = liveNodes.begin(); it != liveNodes.end();)
    {
        if (seenNodes.count (it->first) == 0)
        {
            entries.at (it->second).live = false;
            it = liveNodes.erase (it);
        }
        else
            ++it;
    }

    // Wires ----------------------------------------------------------------------------
    // Accepted wires, as node-key edges, for loop checks.
    std::multimap<Key, Key> edges;

    auto reachesKey = [&edges] (Key from, Key to)
    {
        std::vector<Key> stack { from };
        std::set<Key> seen { from };

        while (! stack.empty())
        {
            const auto current = stack.back();
            stack.pop_back();

            if (current == to)
                return true;

            auto [begin, end] = edges.equal_range (current);
            for (auto e = begin; e != end; ++e)
                if (seen.insert (e->second).second)
                    stack.push_back (e->second);
        }

        return false;
    };

    std::map<WireId, WireEntry> accepted;
    std::set<std::tuple<Key, int, Key, int>> connections;
    std::set<WireId> kept;  // wires that kept their state from the last build

    for (const auto& w : desc.wires)
    {
        const auto src = liveNodes.find (w.sourceNode);
        const auto dst = liveNodes.find (w.destNode);

        if (src == liveNodes.end() || dst == liveNodes.end() || src->second == dst->second || accepted.count (w.id) != 0)
            continue;

        const auto& srcLayout = entries.at (src->second).layout;
        const auto& dstLayout = entries.at (dst->second).layout;

        if (w.sourcePort < 0 || w.sourcePort >= (int) srcLayout.outputs.size()
            || w.destPort < 0 || w.destPort >= (int) dstLayout.inputs.size())
            continue;

        const auto connection = std::make_tuple (src->second, w.sourcePort, dst->second, w.destPort);

        if (connections.count (connection) != 0 || reachesKey (dst->second, src->second))
            continue;

        WireEntry entry { src->second, dst->second, w.sourcePort, w.destPort, nullptr };

        const auto previous = liveWires.find (w.id);

        if (previous != liveWires.end()
            && previous->second.source == entry.source && previous->second.dest == entry.dest
            && previous->second.sourcePort == entry.sourcePort && previous->second.destPort == entry.destPort)
        {
            entry.state = previous->second.state;
            liveWires.erase (previous);
            kept.insert (w.id);
        }
        else
        {
            entry.state = std::make_shared<engine::WireState>();
        }

        entry.state->setGain (std::isfinite (w.gain) ? std::max (0.0f, w.gain) : 1.0f);

        connections.insert (connection);
        edges.emplace (entry.source, entry.dest);
        accepted.emplace (w.id, std::move (entry));
    }

    compensateLatency (accepted, kept);

    // Whatever is left over was removed or changed.
    for (auto& [id, wire] : liveWires)
        retireWire (std::move (wire));

    liveWires = std::move (accepted);

    // A fading wire that would close a loop with the new wiring is cut straight away.
    std::erase_if (fadingWires, [&] (const WireEntry& w)
    {
        if (reachesKey (w.dest, w.source))
            return true;

        edges.emplace (w.source, w.dest);
        return false;
    });

    dropUnusedRetiredNodes();

    return rebuild();
}

void GraphBuilder::compensateLatency (std::map<WireId, WireEntry>& wires, const std::set<WireId>& kept)
{
    // Latency where each node's signal arrives (the latest of its inputs) and where it leaves.
    std::map<Key, int> arrives, leaves;
    std::map<Key, int> inDegree;

    for (const auto& [id, key] : liveNodes)
        inDegree[key] = 0;

    for (const auto& [id, w] : wires)
        ++inDegree[w.dest];

    std::vector<Key> ready;
    for (const auto& [key, degree] : inDegree)
        if (degree == 0)
            ready.push_back (key);

    while (! ready.empty())
    {
        const auto key = ready.back();
        ready.pop_back();
        leaves[key] = arrives[key] + std::max (0, entries.at (key).processor->getLatencySamples());

        for (const auto& [id, w] : wires)
        {
            if (w.source != key)
                continue;

            arrives[w.dest] = std::max (arrives[w.dest], leaves[key]);

            if (--inDegree[w.dest] == 0)
                ready.push_back (w.dest);
        }
    }

    // Recorders line up with each other, so raw and processed tracks of a take match.
    // (Hardware outputs aren't delayed to match each other: that would add latency live.)
    int recorders = 0;
    for (const auto& [id, key] : liveNodes)
        if (entries.at (key).type->id == nodes::types::recorder)
            recorders = std::max (recorders, arrives[key]);

    for (const auto& [id, key] : liveNodes)
        if (entries.at (key).type->id == nodes::types::recorder)
        {
            arrives[key] = recorders;
            leaves[key] = recorders + std::max (0, entries.at (key).processor->getLatencySamples());
        }

    pathLatency.clear();
    for (const auto& [id, key] : liveNodes)
        pathLatency[id] = leaves[key];

    // Each wire delays its signal by however much earlier it arrives than the latest input.
    for (auto& [id, w] : wires)
    {
        const auto needed = std::clamp (arrives[w.dest] - leaves[w.source], 0, maxCompensationSamples);

        if (w.state->getDelaySamples() == needed)
            continue;

        // A new delay needs a new delay line: the old one fades out as the new one fades in.
        if (kept.count (id) != 0)
            retireWire (w);

        const auto channels = entries.at (w.source).layout.outputs[(size_t) w.sourcePort].channels;
        w.state = std::make_shared<engine::WireState> (w.state->getGain(), needed, channels, maxBlock);
    }
}

std::unique_ptr<engine::CompiledGraph> GraphBuilder::rebuild() const
{
    struct Edge
    {
        const WireEntry* wire;
        bool fading;
    };

    std::vector<Edge> all;
    for (const auto& [id, w] : liveWires)
        all.push_back ({ &w, false });
    for (const auto& w : fadingWires)
        all.push_back ({ &w, true });

    // Topological sort (Kahn). Live wires can't form a loop (build() checks), but a
    // fading wire together with live ones can, e.g. A→B replaced by B→A. If so, fading
    // wires between the nodes left over are dropped; they are cut rather than faded.
    std::vector<Key> order;
    std::vector<bool> included (all.size(), true);

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::map<Key, int> inDegree;
        for (const auto& [key, e] : entries)
            inDegree[key] = 0;

        for (size_t i = 0; i < all.size(); ++i)
            if (included[i])
                ++inDegree[all[i].wire->dest];

        std::vector<Key> ready;
        for (const auto& [key, degree] : inDegree)
            if (degree == 0)
                ready.push_back (key);

        order.clear();

        while (! ready.empty())
        {
            const auto key = ready.front();
            ready.erase (ready.begin());
            order.push_back (key);

            for (size_t i = 0; i < all.size(); ++i)
                if (included[i] && all[i].wire->source == key && --inDegree[all[i].wire->dest] == 0)
                    ready.push_back (all[i].wire->dest);
        }

        if (order.size() == entries.size())
            break;

        std::set<Key> sorted (order.begin(), order.end());

        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto inLoop = sorted.count (all[i].wire->source) == 0 && sorted.count (all[i].wire->dest) == 0;

            if (inLoop && (all[i].fading || attempt > 0))
                included[i] = false;
        }
    }

    // Buffers: one per port. -----------------------------------------------------------
    auto graph = std::make_unique<engine::CompiledGraph> (maxBlock);
    std::map<Key, std::vector<int>> outputBuffers;
    std::map<Key, size_t> nodeIndex;

    for (auto key : order)
    {
        const auto& entry = entries.at (key);
        engine::CompiledGraph::Node node;
        node.processor = entry.processor;

        for (const auto& port : entry.layout.inputs)
            node.inputBuffers.push_back (graph->addBuffer (port.channels));

        for (const auto& port : entry.layout.outputs)
            node.outputBuffers.push_back (graph->addBuffer (port.channels));

        node.wiresPerInput.resize (entry.layout.inputs.size());

        for (size_t i = 0; i < all.size(); ++i)
        {
            if (! included[i] || all[i].wire->dest != key)
                continue;

            const auto& w = *all[i].wire;
            const auto& sourceBuffers = outputBuffers.at (w.source);
            node.wiresPerInput[(size_t) w.destPort].push_back ({ w.state, sourceBuffers[(size_t) w.sourcePort] });
        }

        outputBuffers[key] = node.outputBuffers;
        graph->addNode (std::move (node));
    }

    return graph;
}

bool GraphBuilder::setParameter (NodeId node, int paramIndex, float value)
{
    const auto it = liveNodes.find (node);
    if (it == liveNodes.end())
        return false;

    auto& entry = entries.at (it->second);

    if (paramIndex < 0 || paramIndex >= (int) entry.type->params.size() || entry.type->params[(size_t) paramIndex].structural)
        return false;

    auto params = entry.params;
    params[(size_t) paramIndex] = value;
    entry.params = entry.type->sanitise (params);
    entry.processor->setParameter (paramIndex, entry.params[(size_t) paramIndex]);
    return true;
}

bool GraphBuilder::setWireGain (WireId wire, float linearGain)
{
    const auto it = liveWires.find (wire);
    if (it == liveWires.end() || ! std::isfinite (linearGain))
        return false;

    it->second.state->setGain (std::max (0.0f, linearGain));
    return true;
}

engine::NodeProcessor* GraphBuilder::getProcessor (NodeId node) const
{
    const auto it = liveNodes.find (node);
    return it != liveNodes.end() ? entries.at (it->second).processor.get() : nullptr;
}

int GraphBuilder::getPathLatency (NodeId node) const
{
    const auto it = pathLatency.find (node);
    return it != pathLatency.end() ? it->second : 0;
}

int GraphBuilder::getWireDelay (WireId wire) const
{
    const auto it = liveWires.find (wire);
    return it != liveWires.end() ? it->second.state->getDelaySamples() : 0;
}

const nodes::PortLayout* GraphBuilder::getLayout (NodeId node) const
{
    const auto it = liveNodes.find (node);
    return it != liveNodes.end() ? &entries.at (it->second).layout : nullptr;
}

bool GraphBuilder::pruneFinishedFades (bool audioRunning)
{
    const auto before = fadingWires.size();

    std::erase_if (fadingWires, [audioRunning] (const WireEntry& w) { return ! audioRunning || w.state->hasFadedOut(); });

    const auto retiredBefore = getNumRetiredNodes();
    dropUnusedRetiredNodes();

    return fadingWires.size() != before || getNumRetiredNodes() != retiredBefore;
}

} // namespace spm::graph
