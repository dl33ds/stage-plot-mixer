// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ui/FaceComponent.h"

namespace spm::ui
{

/** Adds a face for a node to a panel, to the right of what's there. */
void addFaceToPanel (model::Session& session, juce::int64 panelId, graph::NodeId node);

/** A panel: faces and face groups on a scrollable surface.

    - Drag a face by its name to move it; drop it on a face group to put it in the group.
    - Right-click for faces, face groups, sizes and the panel itself.
    - With Show Lock on, only the controls work.
*/
class PanelView final : public juce::Component,
                        private juce::ValueTree::Listener,
                        private juce::AsyncUpdater,
                        private juce::Timer
{
public:
    PanelView (model::Session& session, const MeterCache& meters, juce::int64 panelId);
    ~PanelView() override;

    juce::int64 getPanelId() const noexcept { return panelId; }
    juce::ValueTree getPanel() const { return session.findPanel (panelId); }

    /** Lets the owner add items to the panel's menu (tear off, back to tabs...). */
    std::function<void (juce::PopupMenu&)> addHostMenuItems;

    /** Asked to show a node in the graph. */
    std::function<void (graph::NodeId)> onShowNode;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Content;
    class FaceGroupComponent;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override { changed (parent); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { changed (parent); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override { changed (parent); }
    void valueTreeRedirected (juce::ValueTree&) override { needsRebuild = true; triggerAsyncUpdate(); }
    void changed (const juce::ValueTree& tree);
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void rebuild();
    void layoutContent();
    bool isLocked() const { return session.isShowLocked(); }

    FaceComponent& makeFace (juce::ValueTree face, juce::Component& parent);

    void faceDown (FaceComponent&, const juce::MouseEvent&);
    void faceDrag (FaceComponent&, const juce::MouseEvent&);
    void faceUp (FaceComponent&, const juce::MouseEvent&);
    void groupDown (FaceGroupComponent&, const juce::MouseEvent&);
    void groupDrag (FaceGroupComponent&, const juce::MouseEvent&);
    void groupUp (FaceGroupComponent&, const juce::MouseEvent&);

    void showPanelMenu (juce::Point<float> contentPoint);
    void showFaceMenu (FaceComponent&);
    void showGroupMenu (FaceGroupComponent&);

    static juce::Point<float> snap (juce::Point<float> p);

    model::Session& session;
    const MeterCache& meters;
    const juce::int64 panelId;

    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    std::vector<std::unique_ptr<FaceGroupComponent>> groups;
    std::vector<std::unique_ptr<FaceComponent>> faces;
    bool needsRebuild = true, needsUpdate = false;

    struct Drag
    {
        juce::Component* component = nullptr;
        juce::Point<float> grabOffset;  // mouse relative to the component's top-left
        bool moved = false;
    };
    Drag drag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelView)
};

} // namespace spm::ui
