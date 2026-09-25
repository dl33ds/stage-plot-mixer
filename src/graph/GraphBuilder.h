// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "engine/CompiledGraph.h"
#include "graph/GraphDesc.h"

#include <map>
#include <memory>

namespace spm::graph
{

/** Turns graph descriptions into compiled graphs, keeping processors and wire states
    alive across rebuilds so edits are seamless. Message thread only.

    - A node that keeps its type and structure keeps its processor (and its state).
    - A structural change (e.g. channel count) gives the node a new processor; the old
      one stays in the graph while its wires fade out, and the new wires fade in.
    - Removed wires fade out; removed nodes stay until their wires have faded.

    Call pruneFinishedFades() regularly (e.g. from a timer); when it returns true,
    submit rebuild() so finished fades leave the running graph.
*/
class GraphBuilder
{
public:
    explicit GraphBuilder (const nodes::NodeRegistry& registry = nodes::NodeRegistry::builtIn(),
                           int maxBlockSize = engine::CompiledGraph::defaultMaxBlockSize);

    /** Applies a description and compiles it. Nodes of unknown type, and wires that are
        invalid or would make a loop, are left out.
    */
    std::unique_ptr<engine::CompiledGraph> build (const GraphDesc& desc);

    /** Compiles the current state again (after pruneFinishedFades). */
    std::unique_ptr<engine::CompiledGraph> rebuild() const;

    /** Changes a non-structural parameter immediately, without a rebuild.
        Returns false if the node doesn't exist or the parameter is structural.
    */
    bool setParameter (NodeId node, int paramIndex, float value);

    bool setWireGain (WireId wire, float linearGain);

    /** The live processor for a node (for meters), or nullptr. */
    engine::NodeProcessor* getProcessor (NodeId node) const;
    const nodes::PortLayout* getLayout (NodeId node) const;

    /** Whether a wire in the last description was accepted. */
    bool isWireActive (WireId wire) const { return liveWires.count (wire) != 0; }

    /** Drops fades that have finished. If audio isn't running, fades can't progress, so
        everything fading is dropped. Returns true if the graph should be rebuilt.
    */
    bool pruneFinishedFades (bool audioRunning);

    bool hasFades() const noexcept { return ! fadingWires.empty(); }

    int getNumFadingWires() const noexcept { return (int) fadingWires.size(); }
    int getNumRetiredNodes() const noexcept;

private:
    using Key = std::uint64_t;  // internal: a node id can map to several keys over time

    struct NodeEntry
    {
        const nodes::NodeType* type = nullptr;
        nodes::ParamValues params;
        nodes::PortLayout layout;
        std::shared_ptr<engine::NodeProcessor> processor;
        bool live = true;
    };

    struct WireEntry
    {
        Key source = 0, dest = 0;
        int sourcePort = 0, destPort = 0;
        std::shared_ptr<engine::WireState> state;
    };

    Key createNode (const nodes::NodeType& type, const nodes::ParamValues& params);
    void retireWire (WireEntry wire);
    void dropUnusedRetiredNodes();

    const nodes::NodeRegistry& registry;
    int maxBlock;
    Key nextKey = 1;

    std::map<Key, NodeEntry> entries;          // live and retired nodes
    std::map<NodeId, Key> liveNodes;
    std::map<WireId, WireEntry> liveWires;
    std::vector<WireEntry> fadingWires;
};

} // namespace spm::graph
