// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "model/Session.h"
#include "ui/Selection.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::ui
{

/** Side panel with every setting of the selected node or wire. */
class Inspector final : public juce::Component,
                        private juce::ValueTree::Listener,
                        private juce::AsyncUpdater,
                        private juce::ChangeListener
{
public:
    Inspector (model::Session& session, Selection& selection);
    ~Inspector() override;

    std::function<juce::StringArray (bool inputs)> getDeviceChannelNames;
    std::function<void()> onDelete, onDuplicate;

    /** While true, a Recorder's channel count is locked (it would end its files). */
    std::function<bool()> isRecording;

    /** Rebuilds the controls, e.g. when recording starts or stops. */
    void refreshAll() { rebuild(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 290;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerAsyncUpdate(); }
    void valueTreeRedirected (juce::ValueTree&) override { triggerAsyncUpdate(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;

    juce::String currentKey() const;
    void rebuild();
    void refreshValues();
    void layoutContent();

    void buildForNode (graph::NodeId);
    void buildForWire (graph::WireId);
    void buildForNodes (int count);
    void buildEmpty();

    struct Row
    {
        std::unique_ptr<juce::Component> label, control;
        int height = 30;
        bool fullWidth = false;
        std::function<void()> refresh;
    };

    juce::Label* addHeading (const juce::String& text);
    juce::Label* addText (const juce::String& text, int height, bool muted = true);
    void addRow (const juce::String& label, std::unique_ptr<juce::Component> control, std::function<void()> refresh, int height = 30);
    void addButtons();

    model::Session& session;
    Selection& selection;
    juce::String builtKey;
    juce::String title, subtitle, iconName;
    juce::Colour titleColour;

    juce::Viewport viewport;
    juce::Component content;
    std::vector<Row> rows;
};

} // namespace spm::ui
