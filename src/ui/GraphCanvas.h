// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "model/Session.h"
#include "model/Templates.h"
#include "ui/Icons.h"
#include "ui/MeterCache.h"
#include "ui/Selection.h"

#include <map>
#include <optional>

namespace spm::ui
{

class NodeComponent;
class QuickAddPanel;

/** A port on a node, for wiring. */
struct PortRef
{
    graph::NodeId node = 0;
    bool input = false;
    int port = 0;

    bool operator== (const PortRef& o) const noexcept { return node == o.node && input == o.input && port == o.port; }
    bool operator< (const PortRef& o) const noexcept { return std::tie (node, input, port) < std::tie (o.node, o.input, o.port); }
};

/** How a port should look while a wire is being dragged. */
enum class PortState { normal, dimmed, available, hovered, refused };

/** The node editor: pannable, zoomable canvas of nodes and wires.

    - Drag from a port to wire it; drop on empty space to add a connected node.
    - Drag from a wired input to move or remove its wire.
    - Middle/right drag or Space+drag pans; the wheel zooms (trackpads pan, pinch zooms).
    - Drag on empty space to select several nodes.
    - Shows one level of groups at a time: double-click a group to go inside, and the
      breadcrumbs (or Escape with nothing selected) to come back out.
*/
class GraphCanvas final : public juce::Component,
                          private juce::ValueTree::Listener,
                          private juce::AsyncUpdater,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    GraphCanvas (model::Session& session, Selection& selection, graph::GraphBuilder& builder);
    ~GraphCanvas() override;

    // View ---------------------------------------------------------------------------------
    void zoomBy (float factor, juce::Point<float> aroundViewPoint);
    void zoomBy (float factor) { zoomBy (factor, getLocalBounds().getCentre().toFloat()); }
    void fitAll();
    float getZoom() const noexcept { return zoom; }
    void setMinimapVisible (bool shouldBeVisible);
    bool isMinimapVisible() const noexcept;

    juce::Point<float> viewToWorld (juce::Point<float> p) const noexcept { return p / zoom + offset; }
    juce::Point<float> worldToView (juce::Point<float> p) const noexcept { return (p - offset) * zoom; }
    juce::Rectangle<float> getVisibleWorldArea() const;

    // Groups -------------------------------------------------------------------------------
    /** The group being shown (0: the top level). */
    graph::NodeId getScope() const noexcept { return scope; }
    void setScope (graph::NodeId group);
    void goUp() { setScope (session.getParent (scope)); }

    // Editing ------------------------------------------------------------------------------
    void deleteSelection();
    void duplicateSelection();
    void selectAll();
    void groupSelection();
    void ungroupSelection();
    void saveAsTemplate (graph::NodeId node);

    /** Show Lock: nothing can be moved, wired, added or removed; controls still work. */
    bool isLocked() const { return session.isShowLocked(); }

    /** Opens the add-node search at a point in the view (or under the mouse). */
    void showQuickAdd (std::optional<juce::Point<float>> viewPoint = {});

    /** Names of the audio interface's channels, for hardware input/output nodes. */
    std::function<juce::StringArray (bool inputs)> getDeviceChannelNames;

    // For node components ------------------------------------------------------------------
    model::Session& getSession() noexcept { return session; }
    Selection& getSelection() noexcept { return selection; }
    const MeterCache& getMeters() const noexcept { return meters; }
    /** Samples of latency the node's processing adds (0 if none, or not built yet). */
    int getNodeLatency (graph::NodeId node) const;
    void clearClips (graph::NodeId node);

    void nodeMouseDown (NodeComponent&, const juce::MouseEvent&);
    void nodeMouseDrag (NodeComponent&, const juce::MouseEvent&);
    void nodeMouseUp (NodeComponent&, const juce::MouseEvent&);
    void portMouseDown (const PortRef&, const juce::MouseEvent&);
    void showNodeMenu (NodeComponent&);

    PortState getPortState (const PortRef&) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Overlay;
    class Minimap;
    class Breadcrumbs;

    // Sync with the session.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeRedirected (juce::ValueTree&) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { sync(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void sync();
    void updateTransforms();
    void keepOverlaysOnTop();
    NodeComponent* findNodeComponent (graph::NodeId) const;

    // Wires.
    struct WireGeometry
    {
        graph::WireId id = 0;
        juce::Path path;
        int channels = 1;
        PortRef source, dest;
    };
    std::vector<WireGeometry> computeWires() const;
    std::optional<juce::Point<float>> getPortPosition (const PortRef&) const;  // view coordinates
    graph::WireId wireAt (juce::Point<float> viewPoint) const;
    static juce::Path makeWirePath (juce::Point<float> from, juce::Point<float> to, float zoom);

    // Wire dragging.
    struct WireDrag
    {
        PortRef from;                       // the fixed end
        juce::Point<float> mouse;           // view coordinates
        std::map<PortRef, std::string> refusals;  // candidate ports that can't take this wire, with why
        std::optional<PortRef> target;      // port under the mouse
        bool pickedUp = false;              // an existing wire was lifted off an input
        int channels = 1;
    };
    std::optional<WireDrag> wireDrag;
    void updateWireDrag (juce::Point<float> viewPoint);
    void finishWireDrag (juce::Point<float> viewPoint);
    std::optional<PortRef> findPortNear (juce::Point<float> viewPoint, bool wantInput) const;

    void showQuickAddFor (juce::Point<float> viewPoint, std::optional<PortRef> connectTo);
    void closeQuickAdd();
    void addNodeFromQuickAdd (const std::string& typeId, juce::Point<float> worldPoint, std::optional<PortRef> connectTo);
    void addFaceMenu (juce::PopupMenu& menu, graph::NodeId node);

    void showCanvasMenu (juce::Point<float> viewPoint);

    model::Session& session;
    Selection& selection;
    graph::GraphBuilder& builder;
    MeterCache meters;

    std::vector<std::unique_ptr<NodeComponent>> nodeComponents;
    graph::NodeId scope = 0;
    std::map<graph::NodeId, std::pair<juce::Point<float>, float>> savedViews;  // offset and zoom per group
    std::vector<model::Template> quickAddTemplates;

    juce::Point<float> offset { -80.0f, -80.0f };  // world point at the view's top-left
    float zoom = 1.0f;
    static constexpr float minZoom = 0.2f, maxZoom = 2.0f;

    enum class DragMode { none, pan, marquee, nodes, wire };
    DragMode dragMode = DragMode::none;
    juce::Point<float> dragStartView, dragStartOffset;
    juce::Rectangle<float> marquee;
    juce::Array<graph::NodeId> marqueeBase;
    std::map<graph::NodeId, juce::Point<float>> dragStartPositions;
    bool dragMoved = false;
    graph::WireId hoveredWire = 0;
    juce::Point<float> lastMouse;

    std::map<graph::WireId, int> wireLevelSteps;  // for repainting wires only when their glow changes
    double lastTick = 0.0;

    std::unique_ptr<Overlay> overlay;
    std::unique_ptr<Minimap> minimap;
    std::unique_ptr<Breadcrumbs> breadcrumbs;
    std::unique_ptr<QuickAddPanel> quickAdd;
    IconButton zoomInButton { "zoom-in", "Zoom in (Ctrl +)" }, zoomOutButton { "zoom-out", "Zoom out (Ctrl -)" },
        fitButton { "maximize", "Fit everything in view (F)" }, mapButton { "map", "Show or hide the overview map" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphCanvas)
};

} // namespace spm::ui
