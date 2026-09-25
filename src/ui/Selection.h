// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "graph/GraphDesc.h"

#include <juce_events/juce_events.h>

namespace spm::ui
{

/** What's selected in the editor: some nodes, or some wires. */
class Selection final : public juce::ChangeBroadcaster
{
public:
    const juce::Array<graph::NodeId>& getNodes() const noexcept { return nodes; }
    const juce::Array<graph::WireId>& getWires() const noexcept { return wires; }
    bool isEmpty() const noexcept { return nodes.isEmpty() && wires.isEmpty(); }

    bool contains (graph::NodeId node) const { return nodes.contains (node); }
    bool containsWire (graph::WireId wire) const { return wires.contains (wire); }

    void selectOnly (graph::NodeId node) { set ({ node }, {}); }
    void selectOnlyWire (graph::WireId wire) { set ({}, { wire }); }
    void clear() { set ({}, {}); }

    void toggle (graph::NodeId node)
    {
        auto next = nodes;
        if (next.contains (node)) next.removeFirstMatchingValue (node);
        else next.add (node);
        set (next, {});
    }

    void set (juce::Array<graph::NodeId> newNodes, juce::Array<graph::WireId> newWires)
    {
        if (newNodes == nodes && newWires == wires)
            return;

        nodes = std::move (newNodes);
        wires = std::move (newWires);
        sendChangeMessage();
    }

    /** Drops anything that no longer exists. */
    template <typename NodeExists, typename WireExists>
    void prune (NodeExists nodeExists, WireExists wireExists)
    {
        auto n = nodes;
        auto w = wires;
        n.removeIf ([&] (auto id) { return ! nodeExists (id); });
        w.removeIf ([&] (auto id) { return ! wireExists (id); });
        set (n, w);
    }

private:
    juce::Array<graph::NodeId> nodes;
    juce::Array<graph::WireId> wires;
};

} // namespace spm::ui
