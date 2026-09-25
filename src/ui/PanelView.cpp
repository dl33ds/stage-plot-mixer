// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui/PanelView.h"

#include "ui/Dialogs.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <algorithm>

namespace spm::ui
{

namespace
{

constexpr int margin = 12, faceGap = 6, groupHeader = 26, groupPad = 6, minGroupWidth = 140, snapStep = 4;

namespace ids = model::ids;

model::FaceSize sizeOf (const juce::ValueTree& face)
{
    return (model::FaceSize) juce::jlimit (0, 2, (int) face.getProperty (ids::size, 1));
}

/** Width of a panel item as laid out (for placing new faces without building components). */
int itemWidth (const juce::ValueTree& item)
{
    if (item.hasType (ids::face))
        return FaceComponent::widthFor (sizeOf (item));

    if ((bool) item[ids::collapsed])
        return minGroupWidth;

    auto width = 2 * groupPad - faceGap;
    for (const auto& face : item)
        width += FaceComponent::widthFor (sizeOf (face)) + faceGap;
    return std::max (minGroupWidth, width);
}

} // namespace

void addFaceToPanel (model::Session& session, juce::int64 panelId, graph::NodeId node)
{
    auto panel = session.findPanel (panelId);
    if (! panel.isValid())
        return;

    auto right = (float) margin;
    for (const auto& item : panel)
        right = std::max (right, (float) item[ids::x] + (float) itemWidth (item) + (float) faceGap * 2.0f);

    session.addFace (panel, node, { right, (float) margin });
}

//==============================================================================
class PanelView::Content final : public juce::Component
{
public:
    explicit Content (PanelView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::canvas);

        if (owner.faces.empty() && owner.groups.empty())
        {
            auto area = owner.viewport.getLocalBounds().reduced (40);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f, theme::Weight::medium));
            g.drawFittedText ("This panel is empty.\n\nRight-click here to add faces - the controls of any node or group - "
                              "or right-click a node in the graph and choose Add face to panel.",
                              area.withSizeKeepingCentre (std::min (area.getWidth(), 420), 120), juce::Justification::centred, 5);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
            owner.showPanelMenu (e.position);
    }

private:
    PanelView& owner;
};

//==============================================================================
class PanelView::FaceGroupComponent final : public juce::Component
{
public:
    FaceGroupComponent (PanelView& o, juce::ValueTree t) : owner (o), tree (t) {}

    juce::ValueTree getTree() const { return tree; }
    bool isCollapsed() const { return (bool) tree[ids::collapsed]; }

    std::vector<FaceComponent*> faces;
    bool dropHighlight = false;

    void layoutFaces()
    {
        auto x = groupPad, height = 60;
        for (auto* f : faces)
        {
            f->setVisible (! isCollapsed());
            f->setTopLeftPosition (x, groupHeader + groupPad);
            x += f->getWidth() + faceGap;
            height = std::max (height, f->getHeight());
        }

        if (isCollapsed())
            setSize (minGroupWidth, groupHeader);
        else
            setSize (std::max (minGroupWidth, x - faceGap + groupPad), groupHeader + groupPad + height + groupPad);
    }

    /** Where a face dropped at this x (in this component) would go among the faces. */
    int insertionIndex (float x) const
    {
        int index = 0;
        for (auto* f : faces)
            if (f->getParentComponent() == this && (float) f->getBounds().getCentreX() < x)
                ++index;
        return index;
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::surface.withAlpha (0.5f));
        g.fillRoundedRectangle (r, theme::radius);
        g.setColour (dropHighlight ? theme::accent : theme::border);
        g.drawRoundedRectangle (r, theme::radius, dropHighlight ? 2.0f : 1.0f);

        auto header = getLocalBounds().removeFromTop (groupHeader).reduced (8, 0);
        drawIcon (g, isCollapsed() ? "chevron-right" : "chevron-down", header.removeFromLeft (14).toFloat().withSizeKeepingCentre (14, 14),
                  theme::textMuted);
        header.removeFromLeft (4);

        g.setColour (theme::text);
        g.setFont (theme::font (12.0f, theme::Weight::semiBold));
        const auto count = tree.getNumChildren();
        g.drawText (tree[ids::name].toString() + (isCollapsed() ? "  (" + juce::String (count) + ")" : juce::String()), header,
                    juce::Justification::centredLeft, true);

        if (! isCollapsed() && faces.empty())
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (11.0f));
            g.drawFittedText ("Drag faces here", getLocalBounds().withTrimmedTop (groupHeader), juce::Justification::centred, 2);
        }
    }

    bool hitChevron (juce::Point<float> p) const { return p.y < (float) groupHeader && p.x < 26.0f; }

    void mouseDown (const juce::MouseEvent& e) override { owner.groupDown (*this, e); }
    void mouseDrag (const juce::MouseEvent& e) override { owner.groupDrag (*this, e); }
    void mouseUp (const juce::MouseEvent& e) override { owner.groupUp (*this, e); }

    void toggleCollapsed()
    {
        owner.session.beginAction (isCollapsed() ? "Expand face group" : "Collapse face group");
        tree.setProperty (ids::collapsed, ! isCollapsed(), &owner.session.getUndoManager());
    }

private:
    PanelView& owner;
    juce::ValueTree tree;
};

//==============================================================================
PanelView::PanelView (model::Session& s, const MeterCache& m, juce::int64 id) : session (s), meters (m), panelId (id)
{
    content = std::make_unique<Content> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    session.getState().addListener (this);
    rebuild();
    startTimerHz (30);
}

PanelView::~PanelView()
{
    session.getState().removeListener (this);
    // Faces are children of groups or the content; drop them before either goes.
    faces.clear();
    groups.clear();
}

void PanelView::paint (juce::Graphics& g)
{
    g.fillAll (theme::canvas);
}

void PanelView::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutContent();
}

void PanelView::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == session.getState())
    {
        if (property == ids::showLock)
            content->repaint();
        return;
    }
    changed (tree);
}

void PanelView::changed (const juce::ValueTree& tree)
{
    const auto panel = getPanel();
    const auto root = session.getState();

    if (tree == root || tree == session.getPanels() || tree == panel || (panel.isValid() && tree.isAChildOf (panel)))
        needsRebuild = true;
    else if (tree.hasType (ids::view) || tree.hasType (ids::layouts) || tree.isAChildOf (root.getChildWithName (ids::view))
             || tree.isAChildOf (root.getChildWithName (ids::layouts)))
        return;
    else
        needsUpdate = true;

    triggerAsyncUpdate();
}

void PanelView::handleAsyncUpdate()
{
    if (drag.component != nullptr && drag.moved)
    {
        triggerAsyncUpdate();  // not while something is being dragged
        return;
    }

    if (needsRebuild)
    {
        rebuild();
    }
    else if (needsUpdate)
    {
        for (auto& f : faces)
            f->update();
        for (auto& g : groups)
            g->layoutFaces();
        layoutContent();
    }

    needsRebuild = needsUpdate = false;
}

void PanelView::timerCallback()
{
    if (! isShowing())
        return;
    for (auto& f : faces)
        f->updateMeter();
}

FaceComponent& PanelView::makeFace (juce::ValueTree tree, juce::Component& parent)
{
    auto face = std::make_unique<FaceComponent> (session, meters, tree);
    face->onHeaderDown = [this] (FaceComponent& f, const juce::MouseEvent& e) { faceDown (f, e); };
    face->onHeaderDrag = [this] (FaceComponent& f, const juce::MouseEvent& e) { faceDrag (f, e); };
    face->onHeaderUp = [this] (FaceComponent& f, const juce::MouseEvent& e) { faceUp (f, e); };
    parent.addAndMakeVisible (*face);
    faces.push_back (std::move (face));
    return *faces.back();
}

void PanelView::rebuild()
{
    drag = {};
    faces.clear();
    groups.clear();

    const auto panel = getPanel();
    const auto nodeExists = [this] (const juce::ValueTree& face)
    {
        return session.findNode ((graph::NodeId) (juce::int64) face[ids::node]).isValid();
    };

    for (const auto& item : panel)
    {
        const auto position = juce::Point<float> ((float) item[ids::x], (float) item[ids::y]).roundToInt();

        if (item.hasType (ids::face) && nodeExists (item))
        {
            makeFace (item, *content).setTopLeftPosition (position);
        }
        else if (item.hasType (ids::faceGroup))
        {
            auto group = std::make_unique<FaceGroupComponent> (*this, item);
            content->addAndMakeVisible (*group);
            for (const auto& f : item)
                if (f.hasType (ids::face) && nodeExists (f))
                    group->faces.push_back (&makeFace (f, *group));
            group->layoutFaces();
            group->setTopLeftPosition (position);
            groups.push_back (std::move (group));
        }
    }

    // Groups behind loose faces, so a face dragged over a group stays visible.
    for (auto& g : groups)
        g->toBack();

    layoutContent();
    content->repaint();
}

void PanelView::layoutContent()
{
    auto bounds = juce::Rectangle<int>();
    for (auto* c : content->getChildren())
        bounds = bounds.getUnion (c->getBounds());

    content->setSize (std::max (viewport.getMaximumVisibleWidth(), bounds.getRight() + margin * 4),
                      std::max (viewport.getMaximumVisibleHeight(), bounds.getBottom() + margin * 4));
}

juce::Point<float> PanelView::snap (juce::Point<float> p)
{
    const auto step = (float) snapStep;
    return { std::max (0.0f, std::round (p.x / step) * step), std::max (0.0f, std::round (p.y / step) * step) };
}

//==============================================================================
void PanelView::faceDown (FaceComponent& face, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showFaceMenu (face);
        return;
    }

    drag = {};
    if (! isLocked() && e.position.y < (float) FaceComponent::headerHeight)
        drag = { &face, e.position, false };
}

void PanelView::faceDrag (FaceComponent& face, const juce::MouseEvent& e)
{
    if (drag.component != &face)
        return;

    if (! drag.moved)
    {
        if (e.getDistanceFromDragStart() < 4)
            return;

        // Lift the face out of its group, onto the surface, so it can go anywhere.
        drag.moved = true;
        const auto topLeft = content->getLocalPoint (face.getParentComponent(), face.getPosition());
        content->addAndMakeVisible (face);
        face.setTopLeftPosition (topLeft);
        face.toFront (false);
    }

    const auto mouse = e.getEventRelativeTo (content.get()).position;
    face.setTopLeftPosition ((mouse - drag.grabOffset).roundToInt());

    for (auto& g : groups)
    {
        const auto over = g->getBounds().toFloat().contains (mouse);
        if (over != g->dropHighlight)
        {
            g->dropHighlight = over;
            g->repaint();
        }
    }
}

void PanelView::faceUp (FaceComponent& face, const juce::MouseEvent& e)
{
    if (drag.component != &face || ! drag.moved)
    {
        drag = {};
        return;
    }

    drag = {};
    const auto mouse = e.getEventRelativeTo (content.get()).position;
    const auto panel = getPanel();

    FaceGroupComponent* target = nullptr;
    for (auto& g : groups)
    {
        g->dropHighlight = false;
        g->repaint();
        if (g->getBounds().toFloat().contains (mouse))
            target = g.get();
    }

    auto tree = face.getTree();
    session.beginAction ("Move face");

    if (target != nullptr && ! target->isCollapsed())
    {
        auto groupTree = target->getTree();
        const auto index = target->insertionIndex (mouse.x - (float) target->getX());
        session.moveFaceItem (tree, groupTree, {});
        groupTree.moveChild (groupTree.indexOf (tree), juce::jmin (index, groupTree.getNumChildren() - 1), &session.getUndoManager());
    }
    else if (target != nullptr)
    {
        // Dropped on a collapsed group: add at the end.
        session.moveFaceItem (tree, target->getTree(), {});
    }
    else
    {
        session.moveFaceItem (tree, panel, snap (face.getPosition().toFloat()));
    }

    needsRebuild = true;
    triggerAsyncUpdate();
}

void PanelView::groupDown (FaceGroupComponent& group, const juce::MouseEvent& e)
{
    drag = {};

    if (e.mods.isPopupMenu())
    {
        showGroupMenu (group);
        return;
    }

    if (isLocked())
        return;

    if (group.hitChevron (e.position))
    {
        group.toggleCollapsed();
        return;
    }

    if (e.position.y < (float) groupHeader)
        drag = { &group, e.position, false };
}

void PanelView::groupDrag (FaceGroupComponent& group, const juce::MouseEvent& e)
{
    if (drag.component != &group)
        return;

    if (! drag.moved && e.getDistanceFromDragStart() < 4)
        return;

    drag.moved = true;
    const auto mouse = e.getEventRelativeTo (content.get()).position;
    group.setTopLeftPosition ((mouse - drag.grabOffset).roundToInt());
}

void PanelView::groupUp (FaceGroupComponent& group, const juce::MouseEvent&)
{
    const auto moved = drag.component == &group && drag.moved;
    drag = {};
    if (! moved)
        return;

    session.beginAction ("Move face group");
    session.moveFaceItem (group.getTree(), getPanel(), snap (group.getPosition().toFloat()));
    needsRebuild = true;
    triggerAsyncUpdate();
}

//==============================================================================
void PanelView::showPanelMenu (juce::Point<float> contentPoint)
{
    const auto locked = isLocked();
    const auto position = snap (contentPoint);
    juce::PopupMenu menu;

    // Every node that has controls, by where it is ("Drums > Kick").
    std::vector<std::pair<juce::String, graph::NodeId>> nodes;
    for (auto id : session.getNodeIds())
    {
        const auto* type = session.getNodeType (id);
        if (type == nullptr || nodes::isGroupPin (type->id))
            continue;

        juce::String label;
        for (auto g : session.getPath (session.getParent (id)))
            label << session.getNodeName (g) << juce::String::fromUTF8 (" \xe2\x80\xba ");
        nodes.push_back ({ label + session.getNodeName (id), id });
    }
    std::sort (nodes.begin(), nodes.end(), [] (auto& a, auto& b) { return a.first.compareNatural (b.first) < 0; });

    juce::PopupMenu add;
    for (auto& entry : nodes)
        add.addItem (entry.first, [this, id = entry.second, position]
        {
            session.beginAction ("Add face");
            session.addFace (getPanel(), id, position);
        });
    if (nodes.empty())
        add.addItem ("(No nodes yet)", false, false, [] {});

    menu.addSubMenu ("Add face", add, ! locked);
    menu.addItem ("Add face group", ! locked, false, [this, position]
    {
        session.beginAction ("Add face group");
        session.addFaceGroup (getPanel(), "Group " + juce::String ((int) groups.size() + 1), position);
    });

    menu.addSeparator();
    menu.addItem ("Rename panel...", ! locked, false, [this, safe = juce::Component::SafePointer<PanelView> (this)]
    {
        askForText (this, "Rename panel", "Name for this panel:", getPanel()[ids::name].toString(), "Rename",
                    [this, safe] (const juce::String& name)
                    {
                        if (safe == nullptr)
                            return;
                        session.beginAction ("Rename panel");
                        getPanel().setProperty (ids::name, name, &session.getUndoManager());
                    });
    });
    menu.addItem ("Delete panel", ! locked, false, [this]
    {
        session.beginAction ("Delete panel");
        session.removePanel (panelId);
    });

    if (addHostMenuItems)
    {
        menu.addSeparator();
        addHostMenuItems (menu);
    }

    if (locked)
    {
        menu.addSeparator();
        menu.addItem ("Show Lock is on: turn it off in the toolbar to edit", false, false, [] {});
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void PanelView::showFaceMenu (FaceComponent& face)
{
    const auto locked = isLocked();
    auto tree = face.getTree();
    const auto node = face.getNode();
    const auto current = sizeOf (tree);

    juce::PopupMenu sizes;
    const std::pair<model::FaceSize, const char*> options[] = {
        { model::FaceSize::compact, "Compact" }, { model::FaceSize::standard, "Standard" }, { model::FaceSize::large, "Large" }
    };
    for (auto& option : options)
        sizes.addItem (option.second, ! locked, option.first == current, [this, tree, size = option.first]() mutable
        {
            session.beginAction ("Change face size");
            tree.setProperty (ids::size, (int) size, &session.getUndoManager());
        });

    juce::PopupMenu menu;
    menu.addSectionHeader (session.getNodeName (node));
    menu.addSubMenu ("Size", sizes, ! locked);
    if (onShowNode)
        menu.addItem ("Show in graph", [this, node] { onShowNode (node); });
    menu.addSeparator();
    menu.addItem ("Remove face", ! locked, false, [this, tree]() mutable
    {
        session.beginAction ("Remove face");
        tree.getParent().removeChild (tree, &session.getUndoManager());
    });

    if (addHostMenuItems)
    {
        menu.addSeparator();
        addHostMenuItems (menu);
    }

    if (locked)
    {
        menu.addSeparator();
        menu.addItem ("Show Lock is on: turn it off in the toolbar to edit", false, false, [] {});
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void PanelView::showGroupMenu (FaceGroupComponent& group)
{
    const auto locked = isLocked();
    auto tree = group.getTree();
    const auto collapsed = group.isCollapsed();

    juce::PopupMenu menu;
    menu.addSectionHeader (tree[ids::name].toString());
    menu.addItem ("Rename...", ! locked, false, [this, tree, safe = juce::Component::SafePointer<PanelView> (this)]
    {
        askForText (this, "Rename face group", "Name for this group:", tree[ids::name].toString(), "Rename",
                    [this, tree, safe] (const juce::String& name) mutable
                    {
                        if (safe == nullptr)
                            return;
                        session.beginAction ("Rename face group");
                        tree.setProperty (ids::name, name, &session.getUndoManager());
                    });
    });
    menu.addItem (collapsed ? "Expand" : "Collapse", ! locked, false,
                  [safe = juce::Component::SafePointer<FaceGroupComponent> (&group)]
                  {
                      if (safe != nullptr)
                          safe->toggleCollapsed();
                  });
    menu.addSeparator();
    menu.addItem ("Remove group (keep its faces)", ! locked, false, [this, tree]() mutable
    {
        session.beginAction ("Remove face group");
        auto panel = tree.getParent();
        auto x = (float) tree[ids::x] + (float) groupPad;
        const auto y = (float) tree[ids::y] + (float) (groupHeader + groupPad);

        while (tree.getNumChildren() > 0)
        {
            auto face = tree.getChild (0);
            const auto width = (float) FaceComponent::widthFor (sizeOf (face));
            session.moveFaceItem (face, panel, snap ({ x, y }));
            x += width + (float) faceGap;
        }
        panel.removeChild (tree, &session.getUndoManager());
    });

    if (locked)
    {
        menu.addSeparator();
        menu.addItem ("Show Lock is on: turn it off in the toolbar to edit", false, false, [] {});
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

} // namespace spm::ui
