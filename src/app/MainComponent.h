// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "app/AudioEngine.h"
#include "app/RecordingManager.h"
#include "app/SessionController.h"
#include "app/StatusBar.h"
#include "ui/GraphCanvas.h"
#include "ui/Inspector.h"
#include "ui/PanelView.h"

#include <map>

namespace spm::app
{

/** The main window's content: toolbar, tabs (the graph and each panel), node editor,
    inspector and status bar, plus opening and saving sessions. Panels can be torn off
    into their own windows; where everything is gets saved with the session (the view)
    and in named layouts.
*/
class MainComponent final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer,
                            private juce::ValueTree::Listener,
                            private juce::AsyncUpdater
{
public:
    MainComponent (AudioEngine& engine, juce::PropertiesFile& settings);
    ~MainComponent() override;

    /** Asks to save unsaved changes, then runs the action (unless the user cancels). */
    void confirmDiscard (std::function<void()> then);

    /** Saves the view settings (call before quitting). */
    void saveSettings();

    /** Selects the node with this name (for development snapshots). */
    void selectNodeNamed (const juce::String& name);

    /** Adds a Recorder after the node with this name (for development snapshots). */
    void addRecorderAfter (const juce::String& name);

    /** Starts or stops a take (the Record button). */
    void toggleRecording();

    /** Adds a panel with a face for every top-level node, plus a Channel Strip, and shows
        it (for development snapshots).
    */
    void addDemoPanel();
    void addDemoEffects();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override { updateTitle(); }

    static constexpr int toolbarHeight = 44, tabsHeight = 34;

private:
    class Tabs;
    class PanelWindow;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override { panelsChanged (parent); }
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override { panelsChanged (parent); }
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override { panelsChanged (parent); }
    void panelsChanged (const juce::ValueTree&);
    void handleAsyncUpdate() override { syncPanels(); }

    // Tabs, panels and windows.
    void syncPanels();
    void showTab (juce::int64 panelId);  // 0: the graph
    void addPanel();
    void renamePanel (juce::int64 panelId);
    void tearOff (juce::int64 panelId, std::optional<juce::Rectangle<int>> bounds = {});
    void dock (juce::int64 panelId);
    void closeAllPanels();
    std::unique_ptr<ui::PanelView> createPanelView (juce::int64 panelId);
    void showTabMenu (juce::int64 panelId);
    void showNodeInGraph (graph::NodeId node);
    juce::DocumentWindow* getMainWindow() const;

    // View and layouts.
    void captureView();
    void applyView();
    void applyViewSoon();
    void showLayoutsMenu();
    void updateLockButton();
    void checkDisplays();

    void newSession();
    void openSession();
    void save (std::function<void (bool saved)> done = {});
    void saveAs (std::function<void (bool saved)> done = {});
    bool writeTo (const juce::File&);
    bool loadFile (const juce::File&, bool quiet);
    void startWithDefaultSession();
    void setModified (bool);
    void updateTitle();
    void updateButtons();
    void showAudioSettings();
    void showRecordingMenu();
    void updateRecordButtons();
    void checkForInterruptedTakes();
    void showError (const juce::String& title, const juce::String& message);
    juce::StringArray deviceChannelNames (bool inputs) const;
    static juce::File sessionsFolder();

    AudioEngine& engine;
    juce::PropertiesFile& settings;
    model::Session session;
    ui::Selection selection;
    SessionController controller { session, engine };

    ui::GraphCanvas canvas { session, selection, engine.getBuilder() };
    ui::Inspector inspector { session, selection };
    StatusBar statusBar { engine };
    RecordingManager recording { engine, session, settings };

    ui::IconButton newButton { "file-plus", "New session (Ctrl+N)", "New" },
        openButton { "folder-open", "Open a session (Ctrl+O)", "Open" },
        saveButton { "save", "Save (Ctrl+S)", "Save" },
        saveAsButton { "save", "Save under a new name (Ctrl+Shift+S)", "Save As" },
        undoButton { "undo-2", "Undo (Ctrl+Z)" },
        redoButton { "redo-2", "Redo (Ctrl+Shift+Z or Ctrl+Y)" },
        addButton { "plus", "Add a node (Tab)", "Add node" },
        recordButton { "record", "Record every armed Recorder node (Ctrl+R)", "Record" },
        markerButton { "flag", "Drop a marker in the take (M)", "Marker" },
        recordMenuButton { "chevron-down", "Recording options: pre-roll, disk warning, takes folder" },
        muteButton { "volume-2", "Mute or unmute every output (Ctrl+M)", "Outputs on" },
        settingsButton { "settings", "Audio interface, sample rate and buffer size", "Audio settings" };

    ui::IconButton lockButton { "lock-open", "Show Lock: stop anything being moved, rewired or deleted by accident. Controls still work.",
                                "Show Lock" },
        layoutsButton { "layout-dashboard", "Layouts: store where the windows and panels are, and bring it back", "Layouts" };

    std::unique_ptr<Tabs> tabs;
    std::map<juce::int64, std::unique_ptr<ui::PanelView>> dockedPanels;
    std::map<juce::int64, std::unique_ptr<PanelWindow>> panelWindows;
    juce::int64 currentTab = 0;
    juce::Array<juce::Rectangle<int>> displayAreas;
    juce::Rectangle<int> mainDisplayArea;

    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File currentFile;
    bool modified = false;
    bool lastMuted = false;
    juce::Rectangle<int> recordStatusArea;
    juce::String lastRecordStatus;
};

} // namespace spm::app
