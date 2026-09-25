// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ui/GraphCanvas.h"

namespace spm::ui
{

/** One node on the canvas: a header with its name, ports down the sides, the controls
    you use most, and a level meter. Laid out in world units; the canvas scales it.
*/
class NodeComponent final : public juce::Component, public juce::TooltipClient
{
public:
    NodeComponent (GraphCanvas& canvas, graph::NodeId id);
    ~NodeComponent() override;

    graph::NodeId getId() const noexcept { return id; }

    /** Pulls name, position, values and ports from the session. */
    void update();

    /** Repaints the meter if the levels moved. */
    void updateMeter();

    /** Port centre in this component's coordinates, or nothing if there's no such port. */
    std::optional<juce::Point<float>> getPortCentre (bool input, int port) const;
    int getNumPorts (bool input) const noexcept { return (int) (input ? layout.inputs.size() : layout.outputs.size()); }
    int getPortChannels (bool input, int port) const noexcept;

    /** Starts renaming in place. */
    void startRename();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static constexpr int width = 188;
    static constexpr int headerHeight = 30;

private:
    class InlineSlider;
    class InlineToggle;
    class InlineChoice;

    void rebuildControls();
    void updateControlValues();
    int portRowHeight() const noexcept;
    juce::Rectangle<int> portsArea() const;
    juce::Rectangle<int> meterArea() const;
    int meterHeight() const noexcept;
    bool showsGainReduction() const noexcept;
    juce::Rectangle<int> gainReductionArea() const;
    void paintGainReduction (juce::Graphics&, juce::Rectangle<float> area);
    std::optional<PortRef> portAt (juce::Point<float> p) const;
    std::optional<std::pair<bool, int>> meterPort() const;  // (input, port)
    int meterChannels() const;
    const PortLevels* meterLevels() const;
    void paintMeter (juce::Graphics&, juce::Rectangle<float> area);
    void setParamFromControl (int index, float value, bool newGesture);

    GraphCanvas& canvas;
    const graph::NodeId id;
    const nodes::NodeType* type = nullptr;
    nodes::PortLayout layout;
    juce::String name;
    std::optional<PortRef> hoveredPort;
    juce::Point<float> lastMouse;
    int lastMeterStep = std::numeric_limits<int>::min();
    int lastReductionStep = -1;
    int latency = 0;
    bool built = false;

    struct Control
    {
        int param = -1;
        std::unique_ptr<juce::Component> component;
        int height = 0;
    };
    std::vector<Control> controls;
    std::unique_ptr<juce::Component> toggleRow;
    std::vector<std::unique_ptr<InlineToggle>> toggles;
    std::unique_ptr<juce::TextEditor> renameEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeComponent)
};

} // namespace spm::ui
