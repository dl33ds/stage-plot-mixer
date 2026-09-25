// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "model/Session.h"
#include "ui/MeterCache.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <limits>
#include <optional>

namespace spm::ui
{

/** A node's controls as a console-style strip, for panels: knobs, buttons and choices,
    then a big fader with a meter if the node (or a group's last fader) has a level.
    A group's face shows the controls of everything inside it, left to right.
*/
class FaceComponent final : public juce::Component
{
public:
    FaceComponent (model::Session& session, const MeterCache& meters, juce::ValueTree face);
    ~FaceComponent() override;

    juce::ValueTree getTree() const { return face; }
    graph::NodeId getNode() const;

    /** Pulls values and names from the session; rebuilds if the controls changed. */
    void update();
    void updateMeter();

    static int widthFor (model::FaceSize size);
    static constexpr int headerHeight = 24;

    /** The header is the handle for moving the face; the panel handles that. */
    std::function<void (FaceComponent&, const juce::MouseEvent&)> onHeaderDown, onHeaderDrag, onHeaderUp;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Knob;
    class Fader;

    struct Item
    {
        graph::NodeId node = 0;
        int param = -1;
        bool operator== (const Item& o) const noexcept { return node == o.node && param == o.param; }
    };

    struct Control
    {
        Item item;
        std::unique_ptr<juce::Component> component;
        juce::String label;  // under knobs
    };

    std::vector<Item> collectItems() const;
    void collect (graph::NodeId node, std::vector<Item>& items, int depth) const;
    void rebuild (const std::vector<Item>& items);
    const nodes::ParamSpec* specOf (const Item&) const;
    void setParam (const Item&, float value, bool newGesture);
    std::optional<std::pair<bool, int>> meterPort() const;
    const PortLevels* meterLevels() const;
    juce::Rectangle<int> meterArea() const;
    model::FaceSize getSize() const;
    int knobHeight() const;
    int faderHeight() const;

    model::Session& session;
    const MeterCache& meters;
    juce::ValueTree face;
    std::vector<Item> items;
    std::vector<Control> controls;
    Control* fader = nullptr;
    juce::String name;
    int lastMeterStep = std::numeric_limits<int>::min();
    bool built = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceComponent)
};

} // namespace spm::ui
