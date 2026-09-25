// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "app/AudioEngine.h"
#include "app/SessionController.h"
#include "app/StatusBar.h"
#include "ui/GraphCanvas.h"
#include "ui/Inspector.h"

namespace spm::app
{

/** The main window's content: toolbar, node editor, inspector and status bar, plus
    opening and saving sessions.
*/
class MainComponent final : public juce::Component, private juce::ChangeListener, private juce::Timer
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

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override { updateTitle(); }

    static constexpr int toolbarHeight = 44;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

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

    ui::IconButton newButton { "file-plus", "New session (Ctrl+N)", "New" },
        openButton { "folder-open", "Open a session (Ctrl+O)", "Open" },
        saveButton { "save", "Save (Ctrl+S)", "Save" },
        saveAsButton { "save", "Save under a new name (Ctrl+Shift+S)", "Save As" },
        undoButton { "undo-2", "Undo (Ctrl+Z)" },
        redoButton { "redo-2", "Redo (Ctrl+Shift+Z or Ctrl+Y)" },
        addButton { "plus", "Add a node (Tab)", "Add node" },
        muteButton { "volume-2", "Mute or unmute every output (Ctrl+M)", "Outputs on" },
        settingsButton { "settings", "Audio interface, sample rate and buffer size", "Audio settings" };

    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File currentFile;
    bool modified = false;
    bool lastMuted = false;
};

} // namespace spm::app
