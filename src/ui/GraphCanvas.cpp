// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/GraphCanvas.h"

#include "engine/Smoother.h"

#include "ui/NodeComponent.h"
#include "ui/QuickAddPanel.h"
#include "ui/Theme.h"

#include <cmath>

namespace spm::ui
{

namespace
{

constexpr float snapGrid = 8.0f;

float wireWidth (int channels)
{
    return channels <= 1 ? 2.0f : channels == 2 ? 3.0f : 4.0f;
}

float glowFor (const PortLevels* levels)
{
    if (levels == nullptr)
        return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (levels->maxPeakDb() - MeterCache::floorDb) / -MeterCache::floorDb);
}

juce::Point<float> snap (juce::Point<float> p)
{
    return { std::round (p.x / snapGrid) * snapGrid, std::round (p.y / snapGrid) * snapGrid };
}

bool isPanGesture (const juce::MouseEvent& e)
{
    return e.mods.isMiddleButtonDown() || e.mods.isRightButtonDown()
           || (e.mods.isLeftButtonDown() && juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey));
}

} // namespace

//==============================================================================
/** Drawn above the nodes: the wire being dragged, the selection rectangle, hints. */
class GraphCanvas::Overlay final : public juce::Component
{
public:
    explicit Overlay (GraphCanvas& c) : canvas (c) { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        if (canvas.dragMode == DragMode::marquee && ! canvas.marquee.isEmpty())
        {
            g.setColour (theme::accent.withAlpha (0.08f));
            g.fillRect (canvas.marquee);
            g.setColour (theme::accent.withAlpha (0.6f));
            g.drawRect (canvas.marquee, 1.0f);
        }

        if (canvas.wireDrag)
            paintWireDrag (g, *canvas.wireDrag);

        if (canvas.nodeComponents.empty() && canvas.quickAdd == nullptr)
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            g.drawText ("Press Tab or double-click to add a node", getLocalBounds(), juce::Justification::centred, false);
        }
    }

private:
    void paintWireDrag (juce::Graphics& g, const WireDrag& drag)
    {
        const auto fixed = canvas.getPortPosition (drag.from);
        if (! fixed)
            return;

        const auto refusal = drag.target ? drag.refusals.find (*drag.target) : drag.refusals.end();
        const auto refused = refusal != drag.refusals.end();

        auto loose = drag.mouse;
        if (drag.target && ! refused)
            if (auto p = canvas.getPortPosition (*drag.target))
                loose = *p;

        const auto path = drag.from.input ? makeWirePath (loose, *fixed, canvas.zoom) : makeWirePath (*fixed, loose, canvas.zoom);
        const auto colour = refused ? theme::danger : theme::channelColour (drag.channels);

        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (path, juce::PathStrokeType ((wireWidth (drag.channels) + 5.0f) * canvas.zoom));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (std::max (1.5f, wireWidth (drag.channels) * canvas.zoom),
                                                  juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (refused)
        {
            // Why it can't connect, next to the cursor.
            const auto text = juce::String (refusal->second);
            const auto f = theme::font (12.0f, theme::Weight::medium);
            const auto w = juce::GlyphArrangement::getStringWidth (f, text) + 20.0f;
            auto bubble = juce::Rectangle<float> (drag.mouse.x + 14.0f, drag.mouse.y + 14.0f, w, 26.0f);
            bubble = bubble.constrainedWithin (getLocalBounds().toFloat().reduced (4.0f));

            g.setColour (theme::danger.darker (0.6f));
            g.fillRoundedRectangle (bubble, 4.0f);
            g.setColour (theme::danger);
            g.drawRoundedRectangle (bubble, 4.0f, 1.0f);
            g.setColour (theme::text);
            g.setFont (f);
            g.drawText (text, bubble, juce::Justification::centred, false);
        }
    }

    GraphCanvas& canvas;
};

//==============================================================================
/** A small overview of the whole graph; click or drag to move the view. */
class GraphCanvas::Minimap final : public juce::Component
{
public:
    explicit Minimap (GraphCanvas& c) : canvas (c) {}

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::surface.withAlpha (0.92f));
        g.fillRoundedRectangle (bounds, theme::radius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::radius, 1.0f);

        const auto map = mapping();

        for (auto& n : canvas.nodeComponents)
        {
            const auto* type = canvas.session.getNodeType (n->getId());
            const auto colour = theme::categoryColour (type != nullptr ? juce::String (type->category) : juce::String());
            g.setColour (canvas.selection.contains (n->getId()) ? theme::accent : colour.withAlpha (0.7f));
            g.fillRect (n->getBounds().toFloat().transformedBy (map));
        }

        g.setColour (theme::text.withAlpha (0.8f));
        g.drawRect (canvas.getVisibleWorldArea().transformedBy (map), 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { moveTo (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { moveTo (e.position); }

private:
    juce::Rectangle<float> worldArea() const
    {
        auto area = canvas.getVisibleWorldArea();
        for (auto& n : canvas.nodeComponents)
            area = area.getUnion (n->getBounds().toFloat());
        return area.expanded (40.0f);
    }

    juce::AffineTransform mapping() const
    {
        const auto world = worldArea();
        const auto target = getLocalBounds().toFloat().reduced (6.0f);
        const auto scale = std::min (target.getWidth() / world.getWidth(), target.getHeight() / world.getHeight());
        const auto used = world.getCentre() * scale;
        return juce::AffineTransform::scale (scale).translated (target.getCentre() - used);
    }

    void moveTo (juce::Point<float> p)
    {
        const auto world = p.transformedBy (mapping().inverted());
        const auto visible = canvas.getVisibleWorldArea();
        canvas.offset = world - juce::Point<float> (visible.getWidth(), visible.getHeight()) * 0.5f;
        canvas.updateTransforms();
    }

    GraphCanvas& canvas;
};

//==============================================================================
GraphCanvas::GraphCanvas (model::Session& s, Selection& sel, graph::GraphBuilder& b)
    : session (s), selection (sel), builder (b)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    overlay = std::make_unique<Overlay> (*this);
    addAndMakeVisible (*overlay);
    minimap = std::make_unique<Minimap> (*this);
    addAndMakeVisible (*minimap);

    for (auto* button : { &zoomInButton, &zoomOutButton, &fitButton, &mapButton })
        addAndMakeVisible (button);

    zoomInButton.onClick = [this] { zoomBy (1.25f); };
    zoomOutButton.onClick = [this] { zoomBy (0.8f); };
    fitButton.onClick = [this] { fitAll(); };
    mapButton.onClick = [this] { setMinimapVisible (! isMinimapVisible()); };
    mapButton.setHighlightColour (theme::accent);

    session.getState().addListener (this);
    selection.addChangeListener (this);

    sync();
    lastTick = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz (30);
}

GraphCanvas::~GraphCanvas()
{
    selection.removeChangeListener (this);
    session.getState().removeListener (this);
}

//==============================================================================
void GraphCanvas::sync()
{
    const auto ids = session.getNodeIds();

    std::erase_if (nodeComponents, [&ids] (auto& n) { return ! ids.contains (n->getId()); });

    for (auto id : ids)
    {
        if (auto* existing = findNodeComponent (id))
        {
            existing->update();
            continue;
        }

        auto node = std::make_unique<NodeComponent> (*this, id);
        addAndMakeVisible (*node);
        nodeComponents.push_back (std::move (node));
    }

    selection.prune ([this] (auto id) { return session.findNode (id).isValid(); },
                     [this] (auto id) { return session.findWire (id).isValid(); });

    if (hoveredWire != 0 && ! session.findWire (hoveredWire).isValid())
        hoveredWire = 0;

    updateTransforms();
    keepOverlaysOnTop();
}

void GraphCanvas::keepOverlaysOnTop()
{
    overlay->toFront (false);
    minimap->toFront (false);
    for (auto* button : { &zoomInButton, &zoomOutButton, &fitButton, &mapButton })
        button->toFront (false);
    if (quickAdd != nullptr)
        quickAdd->toFront (false);
}

void GraphCanvas::updateTransforms()
{
    const auto transform = juce::AffineTransform::translation (-offset).scaled (zoom);
    for (auto& n : nodeComponents)
        n->setTransform (transform);

    repaint();
}

NodeComponent* GraphCanvas::findNodeComponent (graph::NodeId id) const
{
    for (auto& n : nodeComponents)
        if (n->getId() == id)
            return n.get();
    return nullptr;
}

void GraphCanvas::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Selection changed.
    for (auto& n : nodeComponents)
        n->repaint();
    repaint();
}

void GraphCanvas::clearClips (graph::NodeId node)
{
    meters.clearClips (builder, node);
}

//==============================================================================
void GraphCanvas::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto elapsed = juce::jlimit (0.0, 0.5, now - lastTick);
    lastTick = now;

    meters.poll (builder, session.getNodeIds(), elapsed);

    for (auto& n : nodeComponents)
        n->updateMeter();

    // Wires glow with the level on them; repaint the ones that changed.
    std::map<graph::WireId, int> steps;
    for (auto& w : computeWires())
    {
        const auto step = juce::roundToInt (glowFor (meters.get (w.source.node, false, w.source.port)) * 12.0f);
        steps[w.id] = step;

        const auto old = wireLevelSteps.find (w.id);
        if (old == wireLevelSteps.end() || old->second != step)
            repaint (w.path.getBounds().expanded (10.0f * zoom + 4.0f).getSmallestIntegerContainer());
    }
    wireLevelSteps = std::move (steps);
}

//==============================================================================
std::optional<juce::Point<float>> GraphCanvas::getPortPosition (const PortRef& ref) const
{
    auto* node = findNodeComponent (ref.node);
    if (node == nullptr)
        return {};

    if (auto local = node->getPortCentre (ref.input, ref.port))
        return getLocalPoint (node, *local);

    return {};
}

juce::Path GraphCanvas::makeWirePath (juce::Point<float> from, juce::Point<float> to, float zoom)
{
    const auto dx = std::max (40.0f * zoom, std::abs (to.x - from.x) * 0.5f);
    juce::Path p;
    p.startNewSubPath (from);
    p.cubicTo (from.translated (dx, 0.0f), to.translated (-dx, 0.0f), to);
    return p;
}

std::vector<GraphCanvas::WireGeometry> GraphCanvas::computeWires() const
{
    std::vector<WireGeometry> result;
    const auto wires = session.getState().getChildWithName (model::ids::wires);

    for (const auto& w : wires)
    {
        WireGeometry geometry;
        geometry.id = (graph::WireId) (juce::int64) w[model::ids::id];
        geometry.source = { (graph::NodeId) (juce::int64) w[model::ids::source], false, (int) w[model::ids::sourcePort] };
        geometry.dest = { (graph::NodeId) (juce::int64) w[model::ids::dest], true, (int) w[model::ids::destPort] };

        const auto from = getPortPosition (geometry.source), to = getPortPosition (geometry.dest);
        if (! from || ! to)
            continue;

        if (auto* node = findNodeComponent (geometry.source.node))
            geometry.channels = node->getPortChannels (false, geometry.source.port);

        geometry.path = makeWirePath (*from, *to, zoom);
        result.push_back (std::move (geometry));
    }

    return result;
}

graph::WireId GraphCanvas::wireAt (juce::Point<float> viewPoint) const
{
    graph::WireId best = 0;
    auto bestDistance = 7.0f;

    for (auto& w : computeWires())
    {
        if (! w.path.getBounds().expanded (bestDistance).contains (viewPoint))
            continue;

        juce::Point<float> nearest;
        w.path.getNearestPoint (viewPoint, nearest);
        const auto d = nearest.getDistanceFrom (viewPoint);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = w.id;
        }
    }

    return best;
}

juce::Rectangle<float> GraphCanvas::getVisibleWorldArea() const
{
    return { offset.x, offset.y, (float) getWidth() / zoom, (float) getHeight() / zoom };
}

//==============================================================================
void GraphCanvas::paint (juce::Graphics& g)
{
    g.fillAll (theme::canvas);

    // Dot grid, sparser when zoomed out.
    {
        auto spacing = 16.0f;
        while (spacing * zoom < 12.0f)
            spacing *= 4.0f;

        const auto clip = g.getClipBounds().toFloat();
        const auto worldClip = juce::Rectangle<float> (viewToWorld (clip.getTopLeft()), viewToWorld (clip.getBottomRight()));
        const auto dot = std::max (1.0f, 1.5f * zoom);

        g.setColour (theme::border.withAlpha (0.55f));
        for (auto x = std::floor (worldClip.getX() / spacing) * spacing; x <= worldClip.getRight(); x += spacing)
            for (auto y = std::floor (worldClip.getY() / spacing) * spacing; y <= worldClip.getBottom(); y += spacing)
            {
                const auto p = worldToView ({ x, y });
                g.fillRect (p.x - dot * 0.5f, p.y - dot * 0.5f, dot, dot);
            }
    }

    // Wires, under the nodes.
    const auto clip = g.getClipBounds().toFloat();
    for (auto& w : computeWires())
    {
        const auto area = w.path.getBounds().expanded (12.0f * zoom);
        if (! area.intersects (clip))
            continue;

        const auto glow = glowFor (meters.get (w.source.node, false, w.source.port));
        const auto selected = selection.containsWire (w.id);
        const auto hovered = w.id == hoveredWire;
        const auto width = std::max (1.2f, wireWidth (w.channels) * zoom);
        auto colour = theme::channelColour (w.channels);

        if (selected)
        {
            g.setColour (theme::accent.withAlpha (0.5f));
            g.strokePath (w.path, juce::PathStrokeType (width + 5.0f * zoom));
            colour = theme::accent;
        }
        else if (hovered)
        {
            colour = colour.brighter (0.4f);
        }

        if (glow > 0.02f)
        {
            g.setColour (colour.withAlpha (0.22f * glow));
            g.strokePath (w.path, juce::PathStrokeType (width + 8.0f * zoom * glow));
        }

        g.setColour (colour.withAlpha (0.45f + 0.55f * glow));
        g.strokePath (w.path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Gain that isn't unity, at the middle of the wire.
        const auto gain = (float) session.findWire (w.id)[model::ids::gain];
        if (std::abs (gain - 1.0f) > 1.0e-4f && zoom > 0.45f)
        {
            const auto db = gain <= 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") + "inf"
                                         : juce::String (engine::gainToDecibels (gain), 1);
            const auto mid = w.path.getPointAlongPath (w.path.getLength() * 0.5f);
            const auto f = theme::font (10.5f * std::max (0.8f, zoom), theme::Weight::medium, true);
            const auto text = db + " dB";
            auto badge = juce::Rectangle<float> (juce::GlyphArrangement::getStringWidth (f, text) + 10.0f, f.getHeight() + 4.0f).withCentre (mid);
            g.setColour (theme::surface);
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (colour);
            g.drawRoundedRectangle (badge, 3.0f, 1.0f);
            g.setColour (theme::text);
            g.setFont (f);
            g.drawText (text, badge, juce::Justification::centred, false);
        }
    }
}

void GraphCanvas::resized()
{
    overlay->setBounds (getLocalBounds());

    auto area = getLocalBounds().reduced (12);
    if (minimap->isVisible())
    {
        minimap->setBounds (area.removeFromBottom (130).removeFromRight (200));
        area.removeFromBottom (8);
    }

    auto buttons = area.removeFromBottom (30).removeFromRight (4 * 32);
    for (auto* button : { &zoomOutButton, &zoomInButton, &fitButton, &mapButton })
        button->setBounds (buttons.removeFromLeft (32).reduced (1));
}

void GraphCanvas::setMinimapVisible (bool shouldBeVisible)
{
    minimap->setVisible (shouldBeVisible);
    mapButton.setHighlightColour (shouldBeVisible ? std::optional (theme::accent) : std::nullopt);
    resized();
}

bool GraphCanvas::isMinimapVisible() const noexcept
{
    return minimap->isVisible();
}

//==============================================================================
void GraphCanvas::zoomBy (float factor, juce::Point<float> around)
{
    const auto world = viewToWorld (around);
    zoom = juce::jlimit (minZoom, maxZoom, zoom * factor);
    offset = world - around / zoom;
    updateTransforms();
}

void GraphCanvas::fitAll()
{
    if (nodeComponents.empty() || getWidth() <= 0)
    {
        zoom = 1.0f;
        offset = { -80.0f, -80.0f };
        updateTransforms();
        return;
    }

    auto area = nodeComponents.front()->getBounds().toFloat();
    for (auto& n : nodeComponents)
        area = area.getUnion (n->getBounds().toFloat());

    const auto view = getLocalBounds().toFloat().reduced (48.0f);
    zoom = juce::jlimit (minZoom, 1.0f, std::min (view.getWidth() / area.getWidth(), view.getHeight() / area.getHeight()));
    offset = area.getCentre() - getLocalBounds().toFloat().getCentre() / zoom;
    updateTransforms();
}

//==============================================================================
void GraphCanvas::deleteSelection()
{
    if (! selection.getNodes().isEmpty())
    {
        const auto nodes = selection.getNodes();
        session.beginAction (nodes.size() == 1 ? "Delete node" : "Delete nodes");
        selection.clear();
        session.removeNodes (nodes);
    }
    else if (! selection.getWires().isEmpty())
    {
        const auto wires = selection.getWires();
        session.beginAction (wires.size() == 1 ? "Delete wire" : "Delete wires");
        selection.clear();
        session.removeWires (wires);
    }
}

void GraphCanvas::duplicateSelection()
{
    if (selection.getNodes().isEmpty())
        return;

    session.beginAction ("Duplicate");
    selection.set (session.duplicateNodes (selection.getNodes(), { 32.0f, 32.0f }), {});
}

void GraphCanvas::selectAll()
{
    selection.set (session.getNodeIds(), {});
}

//==============================================================================
void GraphCanvas::mouseDown (const juce::MouseEvent& e)
{
    closeQuickAdd();
    grabKeyboardFocus();
    dragStartView = e.position;
    dragMoved = false;

    if (isPanGesture (e))
    {
        // A right click on a wire selects it (for its menu).
        if (e.mods.isRightButtonDown())
            if (auto wire = wireAt (e.position))
                selection.selectOnlyWire (wire);

        dragMode = DragMode::pan;
        dragStartOffset = offset;
        return;
    }

    if (auto wire = wireAt (e.position))
    {
        selection.selectOnlyWire (wire);
        dragMode = DragMode::none;
        return;
    }

    dragMode = DragMode::marquee;
    marquee = {};
    marqueeBase = (e.mods.isShiftDown() || e.mods.isCommandDown()) ? selection.getNodes() : juce::Array<graph::NodeId>();
    if (marqueeBase.isEmpty())
        selection.clear();
}

void GraphCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (e.position.getDistanceFrom (dragStartView) > 3.0f)
        dragMoved = true;

    switch (dragMode)
    {
        case DragMode::pan:
            offset = dragStartOffset - (e.position - dragStartView) / zoom;
            updateTransforms();
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;

        case DragMode::marquee:
        {
            marquee = juce::Rectangle<float> (dragStartView, e.position);
            auto nodes = marqueeBase;
            for (auto& n : nodeComponents)
                if (n->getBoundsInParent().toFloat().intersects (marquee))
                    nodes.addIfNotAlreadyThere (n->getId());
            selection.set (nodes, {});
            overlay->repaint();
            break;
        }

        case DragMode::wire:
            updateWireDrag (e.position);
            break;

        case DragMode::nodes:
        case DragMode::none:
            break;
    }
}

void GraphCanvas::mouseUp (const juce::MouseEvent& e)
{
    const auto mode = std::exchange (dragMode, DragMode::none);
    setMouseCursor (juce::MouseCursor::NormalCursor);

    if (mode == DragMode::pan && e.mods.isRightButtonDown() && ! dragMoved)
        showCanvasMenu (e.position);

    if (mode == DragMode::marquee)
    {
        marquee = {};
        overlay->repaint();
    }

    if (mode == DragMode::wire)
        finishWireDrag (e.position);
}

void GraphCanvas::mouseMove (const juce::MouseEvent& e)
{
    lastMouse = e.position;
    const auto wire = wireAt (e.position);
    if (wire != hoveredWire)
    {
        hoveredWire = wire;
        repaint();
    }
}

void GraphCanvas::mouseExit (const juce::MouseEvent&)
{
    if (hoveredWire != 0)
    {
        hoveredWire = 0;
        repaint();
    }
}

void GraphCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && wireAt (e.position) == 0)
        showQuickAddFor (e.position, {});
}

void GraphCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto ep = e.getEventRelativeTo (this).position;

    // Trackpads scroll smoothly: pan, and zoom with Ctrl/Cmd (or pinch). Mouse wheels zoom.
    if (wheel.isSmooth && ! (e.mods.isCtrlDown() || e.mods.isCommandDown()))
    {
        offset -= juce::Point<float> (wheel.deltaX, wheel.deltaY) * (500.0f / zoom) * (wheel.isReversed ? -1.0f : 1.0f);
        updateTransforms();
        return;
    }

    const auto delta = (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 1.5f;
    zoomBy (std::pow (2.0f, juce::jlimit (-0.35f, 0.35f, delta)), ep);
}

void GraphCanvas::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomBy (scaleFactor, e.getEventRelativeTo (this).position);
}

bool GraphCanvas::keyPressed (const juce::KeyPress& key)
{
    const auto command = key.getModifiers().isCommandDown();

    if (key == juce::KeyPress::tabKey)
    {
        showQuickAdd();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        if (wireDrag)
        {
            if (wireDrag->pickedUp)
                session.getUndoManager().undoCurrentTransactionOnly();
            wireDrag.reset();
            dragMode = DragMode::none;
            repaint();
        }
        else
        {
            selection.clear();
        }
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (! command && key.getTextCharacter() == 'f')
    {
        fitAll();
        return true;
    }

    if (command && key.getKeyCode() == 'D')
    {
        duplicateSelection();
        return true;
    }

    if (command && key.getKeyCode() == 'A')
    {
        selectAll();
        return true;
    }

    if (command && (key.getTextCharacter() == '=' || key.getTextCharacter() == '+' || key.getKeyCode() == '='))
    {
        zoomBy (1.25f);
        return true;
    }

    if (command && (key.getTextCharacter() == '-' || key.getKeyCode() == '-'))
    {
        zoomBy (0.8f);
        return true;
    }

    return false;
}

//==============================================================================
void GraphCanvas::nodeMouseDown (NodeComponent& node, const juce::MouseEvent& e)
{
    closeQuickAdd();

    grabKeyboardFocus();
    dragStartView = e.position;
    dragMoved = false;

    if (isPanGesture (e))
    {
        if (e.mods.isRightButtonDown() && ! selection.contains (node.getId()))
            selection.selectOnly (node.getId());

        dragMode = DragMode::pan;
        dragStartOffset = offset;
        return;
    }

    const auto id = node.getId();

    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        selection.toggle (id);
        if (! selection.contains (id))
        {
            dragMode = DragMode::none;
            return;
        }
    }
    else if (! selection.contains (id))
    {
        selection.selectOnly (id);
    }

    dragMode = DragMode::nodes;
    dragStartPositions.clear();
    for (auto n : selection.getNodes())
        dragStartPositions[n] = session.getNodePosition (n);

    node.toFront (false);
    keepOverlaysOnTop();
}

void GraphCanvas::nodeMouseDrag (NodeComponent&, const juce::MouseEvent& e)
{
    if (dragMode != DragMode::nodes)
    {
        mouseDrag (e);
        return;
    }

    if (! dragMoved)
    {
        if (e.position.getDistanceFrom (dragStartView) < 3.0f)
            return;

        dragMoved = true;
        session.beginAction (dragStartPositions.size() == 1 ? "Move node" : "Move nodes");
    }

    const auto delta = (e.position - dragStartView) / zoom;
    const auto free = e.mods.isAltDown();

    for (auto& [id, start] : dragStartPositions)
    {
        const auto p = start + delta;
        session.setNodePosition (id, free ? p : snap (p));
        if (auto* n = findNodeComponent (id))
            n->update();
    }

    repaint();
}

void GraphCanvas::nodeMouseUp (NodeComponent& node, const juce::MouseEvent& e)
{
    if (dragMode == DragMode::nodes)
    {
        dragMode = DragMode::none;

        // A click (no drag) on one node of a selection selects just that node.
        if (! dragMoved && ! (e.mods.isShiftDown() || e.mods.isCommandDown()))
            selection.selectOnly (node.getId());
        return;
    }

    if (dragMode == DragMode::pan && e.mods.isRightButtonDown() && ! dragMoved)
    {
        dragMode = DragMode::none;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        showNodeMenu (node);
        return;
    }

    mouseUp (e);
}

//==============================================================================
void GraphCanvas::portMouseDown (const PortRef& port, const juce::MouseEvent& e)
{
    closeQuickAdd();

    grabKeyboardFocus();

    WireDrag drag;
    drag.from = port;
    drag.mouse = e.position;

    if (port.input)
    {
        // Lift the most recent wire off this input, if there is one.
        juce::ValueTree latest;
        for (const auto& w : session.getState().getChildWithName (model::ids::wires))
            if ((graph::NodeId) (juce::int64) w[model::ids::dest] == port.node && (int) w[model::ids::destPort] == port.port)
                if (! latest.isValid() || (juce::int64) w[model::ids::id] > (juce::int64) latest[model::ids::id])
                    latest = w;

        if (latest.isValid())
        {
            drag.from = { (graph::NodeId) (juce::int64) latest[model::ids::source], false, (int) latest[model::ids::sourcePort] };
            drag.pickedUp = true;
            session.beginAction ("Move wire");
            session.removeWires ({ (graph::WireId) (juce::int64) latest[model::ids::id] });
        }
    }

    if (auto* fromNode = findNodeComponent (drag.from.node))
        drag.channels = std::max (1, fromNode->getPortChannels (drag.from.input, drag.from.port));

    // Work out once which ports can take this wire, and why the others can't.
    for (auto& n : nodeComponents)
        for (int p = 0; p < n->getNumPorts (! drag.from.input); ++p)
        {
            const auto reason = drag.from.input ? session.checkWire (n->getId(), p, drag.from.node, drag.from.port)
                                                : session.checkWire (drag.from.node, drag.from.port, n->getId(), p);
            if (! reason.empty())
                drag.refusals[{ n->getId(), ! drag.from.input, p }] = reason;
        }

    wireDrag = std::move (drag);
    dragMode = DragMode::wire;
    dragStartView = e.position;
    updateWireDrag (e.position);

    for (auto& n : nodeComponents)
        n->repaint();
}

std::optional<PortRef> GraphCanvas::findPortNear (juce::Point<float> viewPoint, bool wantInput) const
{
    std::optional<PortRef> best;
    auto bestDistance = std::max (14.0f, 18.0f * zoom);

    for (auto& n : nodeComponents)
        for (int p = 0; p < n->getNumPorts (wantInput); ++p)
            if (auto pos = getPortPosition ({ n->getId(), wantInput, p }))
            {
                const auto d = pos->getDistanceFrom (viewPoint);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = PortRef { n->getId(), wantInput, p };
                }
            }

    return best;
}

void GraphCanvas::updateWireDrag (juce::Point<float> viewPoint)
{
    if (! wireDrag)
        return;

    wireDrag->mouse = viewPoint;
    auto target = findPortNear (viewPoint, ! wireDrag->from.input);
    if (target && target->node == wireDrag->from.node)
        target.reset();

    if (target != wireDrag->target)
    {
        auto repaintNode = [this] (const std::optional<PortRef>& ref)
        {
            if (ref)
                if (auto* n = findNodeComponent (ref->node))
                    n->repaint();
        };
        repaintNode (wireDrag->target);
        repaintNode (target);
        wireDrag->target = target;
    }

    overlay->repaint();
}

void GraphCanvas::finishWireDrag (juce::Point<float> viewPoint)
{
    if (! wireDrag)
        return;

    updateWireDrag (viewPoint);
    const auto drag = *wireDrag;
    wireDrag.reset();

    for (auto& n : nodeComponents)
        n->repaint();
    overlay->repaint();

    if (drag.target)
    {
        if (drag.refusals.count (*drag.target) != 0)
        {
            // Couldn't connect there: put a lifted wire back.
            if (drag.pickedUp)
                session.getUndoManager().undoCurrentTransactionOnly();
            return;
        }

        if (! drag.pickedUp)
            session.beginAction ("Connect");

        const auto source = drag.from.input ? *drag.target : drag.from;
        const auto dest = drag.from.input ? drag.from : *drag.target;
        session.addWire (source.node, source.port, dest.node, dest.port);
        return;
    }

    // Dropped a lifted wire on empty space: it's removed. A new wire: offer to add a node.
    if (! drag.pickedUp && viewPoint.getDistanceFrom (dragStartView) > 12.0f)
        showQuickAddFor (viewPoint, drag.from);
}

PortState GraphCanvas::getPortState (const PortRef& ref) const
{
    if (! wireDrag)
        return PortState::normal;

    if (ref == wireDrag->from)
        return PortState::hovered;

    if (ref.input == wireDrag->from.input)
        return PortState::dimmed;

    const auto refused = wireDrag->refusals.count (ref) != 0;

    if (wireDrag->target && *wireDrag->target == ref)
        return refused ? PortState::refused : PortState::hovered;

    return refused ? PortState::dimmed : PortState::available;
}

//==============================================================================
void GraphCanvas::showQuickAdd (std::optional<juce::Point<float>> viewPoint)
{
    auto p = viewPoint.value_or (getLocalBounds().toFloat().contains (lastMouse) && isMouseOver (true)
                                     ? lastMouse : getLocalBounds().getCentre().toFloat());
    showQuickAddFor (p, {});
}

void GraphCanvas::closeQuickAdd()
{
    if (quickAdd == nullptr)
        return;

    // Deleted later: this is often called from the panel's own callbacks.
    quickAdd->setVisible (false);
    juce::MessageManager::callAsync ([old = std::shared_ptr<QuickAddPanel> (std::move (quickAdd))] {});
    overlay->repaint();
}

void GraphCanvas::showQuickAddFor (juce::Point<float> viewPoint, std::optional<PortRef> connectTo)
{
    closeQuickAdd();

    QuickAddPanel::Filter filter;
    if (connectTo)
    {
        const auto needInputs = ! connectTo->input;
        filter = [needInputs] (const nodes::NodeType& type)
        {
            const auto layout = type.layout (type.defaults());
            return needInputs ? ! layout.inputs.empty() : ! layout.outputs.empty();
        };
    }

    const auto world = viewToWorld (viewPoint);

    quickAdd = std::make_unique<QuickAddPanel> (
        connectTo ? "Add and connect" : "Add node", filter,
        [this, world, connectTo] (const std::string& typeId)
        {
            closeQuickAdd();
            addNodeFromQuickAdd (typeId, world, connectTo);
            grabKeyboardFocus();
        },
        [this] { closeQuickAdd(); });

    const auto size = juce::Point<int> (QuickAddPanel::panelWidth, quickAdd->getIdealHeight());
    auto bounds = juce::Rectangle<int> (size.x, size.y).withPosition (viewPoint.toInt() + juce::Point<int> (8, 8));
    quickAdd->setBounds (bounds.constrainedWithin (getLocalBounds().reduced (8)));
    addAndMakeVisible (*quickAdd);
    quickAdd->focusSearch();
    overlay->repaint();
}

void GraphCanvas::addNodeFromQuickAdd (const std::string& typeId, juce::Point<float> world, std::optional<PortRef> connectTo)
{
    const auto* type = nodes::NodeRegistry::builtIn().find (typeId);
    if (type == nullptr)
        return;

    auto params = type->defaults();
    auto position = world;

    if (connectTo)
    {
        // Match the channel count of what it's being connected to.
        auto channels = 1;
        if (auto* n = findNodeComponent (connectTo->node))
            channels = n->getPortChannels (connectTo->input, connectTo->port);

        for (size_t i = 0; i < type->params.size(); ++i)
        {
            const auto& spec = type->params[i];
            if (spec.structural && (spec.id == "channels" || spec.id == "inputs"))
                params[i] = juce::jlimit (spec.minValue, spec.maxValue, (float) channels);
        }

        // Put the new node's port where the wire was dropped.
        position.y -= (float) NodeComponent::headerHeight + 17.0f;
        if (connectTo->input)
            position.x -= (float) NodeComponent::width;
    }

    session.beginAction ("Add " + juce::String (type->name));
    const auto id = session.addNode (typeId, snap (position), params);

    if (connectTo)
    {
        const auto layout = session.getLayout (id);

        if (connectTo->input)
        {
            for (int p = 0; p < (int) layout.outputs.size(); ++p)
                if (session.addWire (id, p, connectTo->node, connectTo->port) != 0)
                    break;
        }
        else
        {
            for (int p = 0; p < (int) layout.inputs.size(); ++p)
                if (session.addWire (connectTo->node, connectTo->port, id, p) != 0)
                    break;
        }
    }

    selection.selectOnly (id);
}

//==============================================================================
void GraphCanvas::showCanvasMenu (juce::Point<float> viewPoint)
{
    juce::PopupMenu menu;

    if (! selection.getWires().isEmpty())
    {
        const auto wire = selection.getWires().getFirst();
        menu.addItem ("Delete wire", [this] { deleteSelection(); });
        menu.addItem ("Reset gain to 0 dB", [this, wire]
        {
            session.beginAction ("Reset wire gain");
            session.setWireGain (wire, 1.0f);
        });
        menu.addSeparator();
    }

    menu.addItem ("Add node...", [this, viewPoint] { showQuickAddFor (viewPoint, {}); });
    menu.addItem ("Select all", [this] { selectAll(); });
    menu.addItem ("Fit to view", [this] { fitAll(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void GraphCanvas::showNodeMenu (NodeComponent& node)
{
    const auto id = node.getId();
    const auto several = selection.getNodes().size() > 1;
    juce::PopupMenu menu;

    menu.addItem ("Rename", ! several, false,
                  [safe = juce::Component::SafePointer<NodeComponent> (&node)]
                  {
                      if (safe != nullptr)
                          safe->startRename();
                  });
    menu.addItem (several ? "Duplicate nodes" : "Duplicate", [this] { duplicateSelection(); });
    menu.addItem ("Disconnect all", [this, id]
    {
        juce::Array<graph::WireId> wires;
        for (const auto& w : session.getState().getChildWithName (model::ids::wires))
            if ((graph::NodeId) (juce::int64) w[model::ids::source] == id || (graph::NodeId) (juce::int64) w[model::ids::dest] == id)
                wires.add ((graph::WireId) (juce::int64) w[model::ids::id]);

        session.beginAction ("Disconnect");
        session.removeWires (wires);
    });
    menu.addSeparator();
    menu.addItem (several ? "Delete nodes" : "Delete", [this] { deleteSelection(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

} // namespace spm::ui
