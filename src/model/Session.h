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
    inline const juce::Identifier id { "id" }, type { "type" }, name { "name" }, x { "x" }, y { "y" }, parent { "parent" };
    inline const juce::Identifier source { "source" }, sourcePort { "sourcePort" }, dest { "dest" }, destPort { "destPort" };
    inline const juce::Identifier gain { "gain" }, nextId { "nextId" }, version { "version" }, showLock { "showLock" };

    // Panels: surfaces of faces. A Panel holds Faces and FaceGroups; a FaceGroup holds Faces.
    inline const juce::Identifier panels { "Panels" }, panel { "Panel" }, face { "Face" }, faceGroup { "FaceGroup" };
    inline const juce::Identifier size { "size" }, collapsed { "collapsed" };

    // Where things are on screen: the current View, and named Layouts saved from it.
    inline const juce::Identifier view { "View" }, layouts { "Layouts" }, layout { "Layout" }, window { "Window" };
    inline const juce::Identifier tab { "tab" }, bounds { "bounds" }, display { "display" }, maximised { "maximised" };
    inline const juce::Identifier mainBounds { "mainBounds" }, mainDisplay { "mainDisplay" }, current { "current" };
}

/** Face sizes, stored in Face.size. */
enum class FaceSize { compact = 0, standard = 1, large = 2 };

/** The document: nodes (with position, name and parameters) and wires, in a ValueTree
    with undo. Message thread only. Parameters are stored by id ("p.level"), so files
    survive parameters being added or reordered.

    Groups: every node has a parent (0 for the top level, otherwise a group node). Wires
    only join nodes with the same parent. A group's ports are the Group Input and Group
    Output pins inside it, in the order they were added. For the audio engine the groups
    are flattened away: toGraphDesc() wires straight to the pins.

    Panels, the view (windows) and layouts are stored with the document too. Panel edits
    are undoable; the view, layouts and Show Lock aren't.
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
    /** Adds a node in a group (0: the top level). Pins can only go in a group. */
    graph::NodeId addNode (std::string_view type, juce::Point<float> position, const nodes::ParamValues& params = {},
                           graph::NodeId parent = 0);
    void removeNodes (const juce::Array<graph::NodeId>& nodeIds);  // with their wires, contents and faces
    juce::Array<graph::NodeId> duplicateNodes (const juce::Array<graph::NodeId>& nodeIds, juce::Point<float> offset);

    juce::ValueTree findNode (graph::NodeId id) const;
    juce::Array<graph::NodeId> getNodeIds() const;  // every node, at every level

    void setNodePosition (graph::NodeId id, juce::Point<float> position);
    juce::Point<float> getNodePosition (graph::NodeId id) const;
    void setNodeName (graph::NodeId id, const juce::String& name);
    juce::String getNodeName (graph::NodeId id) const;
    const nodes::NodeType* getNodeType (graph::NodeId id) const;

    nodes::ParamValues getParams (graph::NodeId id) const;
    float getParam (graph::NodeId id, int index) const;
    void setParam (graph::NodeId id, int index, float value);

    /** Port layout as shown on the canvas (groups get theirs from their pins), or an
        empty one for unknown node types.
    */
    nodes::PortLayout getLayout (graph::NodeId id) const;

    // Groups -----------------------------------------------------------------------------
    graph::NodeId getParent (graph::NodeId id) const;
    juce::Array<graph::NodeId> getChildren (graph::NodeId parent) const;  // 0: the top level
    bool isGroup (graph::NodeId id) const;
    bool isInside (graph::NodeId id, graph::NodeId ancestor) const;  // at any depth

    /** A group's input or output pins, in port order. */
    juce::Array<graph::NodeId> getPins (graph::NodeId group, bool inputs) const;

    /** The groups from the top level down to this one (inclusive); empty for 0. */
    juce::Array<graph::NodeId> getPath (graph::NodeId group) const;

    /** Moves nodes (which must share a parent) into a new group, adding pins for the
        wires that cross its edge so the sound is unchanged. Returns the group, or 0.
    */
    graph::NodeId groupNodes (const juce::Array<graph::NodeId>& nodeIds, const juce::String& name = "Group");

    /** Moves a group's contents out to its parent, rewiring through its pins, and removes it. */
    void ungroup (graph::NodeId group);

    /** A node and everything inside it, with the wires among them (for templates). */
    juce::var exportNodes (graph::NodeId root) const;

    /** Adds a copy of exported nodes (new ids) into a group. Returns the new root, or 0. */
    graph::NodeId importNodes (const juce::var& exported, graph::NodeId parent, juce::Point<float> position);

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

    // Panels -----------------------------------------------------------------------------
    juce::ValueTree getPanels() const { return state.getChildWithName (ids::panels); }
    juce::int64 addPanel (const juce::String& name);
    void removePanel (juce::int64 panelId);
    juce::ValueTree findPanel (juce::int64 panelId) const;

    /** Adds a face to a panel or face group. */
    juce::ValueTree addFace (juce::ValueTree container, graph::NodeId node, juce::Point<float> position,
                             FaceSize size = FaceSize::standard);
    juce::ValueTree addFaceGroup (juce::ValueTree panel, const juce::String& name, juce::Point<float> position);

    /** Moves a face or face group to another container (panel or face group), at a position. */
    void moveFaceItem (juce::ValueTree item, juce::ValueTree newContainer, juce::Point<float> position);

    // Show Lock, view and layouts --------------------------------------------------------
    bool isShowLocked() const { return (bool) state[ids::showLock]; }
    void setShowLocked (bool locked) { state.setProperty (ids::showLock, locked, nullptr); }

    /** The windows as they were when the session was last saved. */
    juce::ValueTree getView() const { return state.getChildWithName (ids::view); }
    void setView (const juce::ValueTree& view);

    juce::StringArray getLayoutNames() const;
    void storeLayout (const juce::String& name);  // the current view, under a name
    bool recallLayout (const juce::String& name);  // makes it the current view
    void removeLayout (const juce::String& name);

    // Whole document ---------------------------------------------------------------------
    graph::GraphDesc toGraphDesc() const;

    juce::String toJson() const;
    /** Replaces the document. Returns an error message, or empty on success. Clears undo. */
    juce::String loadJson (const juce::String& json);
    void clear();

    static juce::String paramKey (const nodes::ParamSpec& spec) { return "p." + juce::String (spec.id); }
    static constexpr int fileVersion = 2;

private:
    graph::NodeId takeId();
    juce::ValueTree nodesTree() const { return state.getChildWithName (ids::nodes); }
    juce::ValueTree wiresTree() const { return state.getChildWithName (ids::wires); }
    juce::ValueTree layoutsTree() const { return state.getChildWithName (ids::layouts); }
    juce::String uniqueName (const juce::String& base, graph::NodeId parent) const;
    juce::Array<graph::NodeId> withDescendants (const juce::Array<graph::NodeId>& nodeIds) const;
    void renumberGroupPorts (graph::NodeId group, const juce::Array<graph::NodeId>& removedPins);
    std::string translateWire (graph::WireDesc& wire) const;

    juce::ValueTree state;
    juce::UndoManager undo;
};

} // namespace spm::model
