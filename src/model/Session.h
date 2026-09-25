// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "graph/GraphDesc.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

namespace spm::model
{

namespace ids
{
    inline const juce::Identifier session { "Session" }, nodes { "Nodes" }, wires { "Wires" };
    inline const juce::Identifier node { "Node" }, wire { "Wire" };
    inline const juce::Identifier id { "id" }, type { "type" }, name { "name" }, x { "x" }, y { "y" };
    inline const juce::Identifier source { "source" }, sourcePort { "sourcePort" }, dest { "dest" }, destPort { "destPort" };
    inline const juce::Identifier gain { "gain" }, nextId { "nextId" }, version { "version" };
}

/** The document: nodes (with position, name and parameters) and wires, in a ValueTree
    with undo. Message thread only. Parameters are stored by id ("p.level"), so files
    survive parameters being added or reordered.
*/
class Session
{
public:
    Session();

    juce::ValueTree& getState() noexcept { return state; }
    juce::UndoManager& getUndoManager() noexcept { return undo; }

    /** Starts a new undo step, e.g. at the start of a drag. */
    void beginAction (const juce::String& name) { undo.beginNewTransaction (name); }

    // Nodes ------------------------------------------------------------------------------
    graph::NodeId addNode (std::string_view type, juce::Point<float> position, const nodes::ParamValues& params = {});
    void removeNodes (const juce::Array<graph::NodeId>& nodeIds);  // with their wires
    juce::Array<graph::NodeId> duplicateNodes (const juce::Array<graph::NodeId>& nodeIds, juce::Point<float> offset);

    juce::ValueTree findNode (graph::NodeId id) const;
    juce::Array<graph::NodeId> getNodeIds() const;

    void setNodePosition (graph::NodeId id, juce::Point<float> position);
    juce::Point<float> getNodePosition (graph::NodeId id) const;
    void setNodeName (graph::NodeId id, const juce::String& name);
    juce::String getNodeName (graph::NodeId id) const;
    const nodes::NodeType* getNodeType (graph::NodeId id) const;

    nodes::ParamValues getParams (graph::NodeId id) const;
    float getParam (graph::NodeId id, int index) const;
    void setParam (graph::NodeId id, int index, float value);

    /** Current port layout, or an empty one for unknown node types. */
    nodes::PortLayout getLayout (graph::NodeId id) const;

    // Wires ------------------------------------------------------------------------------
    /** Why a new wire isn't allowed, or empty if it is. */
    std::string checkWire (graph::NodeId source, int sourcePort, graph::NodeId dest, int destPort) const;

    /** Adds a wire if allowed; returns its id, or 0. */
    graph::WireId addWire (graph::NodeId source, int sourcePort, graph::NodeId dest, int destPort, float gain = 1.0f);
    void removeWires (const juce::Array<graph::WireId>& wireIds);
    juce::ValueTree findWire (graph::WireId id) const;
    void setWireGain (graph::WireId id, float linearGain);

    /** Wires whose ports no longer exist (after a channel/port count change) are removed. */
    void removeDanglingWires();

    // Whole document ---------------------------------------------------------------------
    graph::GraphDesc toGraphDesc() const;

    juce::String toJson() const;
    /** Replaces the document. Returns an error message, or empty on success. Clears undo. */
    juce::String loadJson (const juce::String& json);
    void clear();

    static juce::String paramKey (const nodes::ParamSpec& spec) { return "p." + juce::String (spec.id); }
    static constexpr int fileVersion = 1;

private:
    graph::NodeId takeId();
    juce::ValueTree nodesTree() const { return state.getChildWithName (ids::nodes); }
    juce::ValueTree wiresTree() const { return state.getChildWithName (ids::wires); }
    juce::String uniqueName (const juce::String& base) const;

    juce::ValueTree state;
    juce::UndoManager undo;
};

} // namespace spm::model
