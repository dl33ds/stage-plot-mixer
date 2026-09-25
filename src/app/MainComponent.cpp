// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/MainComponent.h"

#include "model/Templates.h"
#include "model/WindowPlacement.h"
#include "ui/Dialogs.h"
#include "ui/Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <set>

namespace spm::app
{

namespace
{
const juce::String fileExtension = ".mixproj";
namespace ids = model::ids;

/** Today's displays (their user areas), the primary first. */
juce::Array<juce::Rectangle<int>> getDisplayAreas()
{
    juce::Array<juce::Rectangle<int>> areas;
    for (const auto& d : juce::Desktop::getInstance().getDisplays().displays)
    {
        if (d.isMain)
            areas.insert (0, d.userBounds.toNearestInt());
        else
            areas.add (d.userBounds.toNearestInt());
    }
    return areas;
}

juce::Rectangle<int> displayAreaFor (juce::Rectangle<int> bounds)
{
    if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (bounds))
        return d->userBounds.toNearestInt();
    return {};
}
} // namespace

//==============================================================================
/** The row of tabs under the toolbar: the graph, each panel, and + for a new one. */
class MainComponent::Tabs final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Tabs (MainComponent& o) : owner (o) {}

    struct Tab
    {
        juce::int64 id = 0;  // 0: the graph; -1: the + tab
        juce::String name;
        bool tornOff = false;
        juce::Rectangle<int> bounds;
    };

    void setTabs (std::vector<Tab> newTabs, juce::int64 current)
    {
        tabs = std::move (newTabs);
        tabs.push_back ({ -1, {}, false, {} });
        active = current;
        layout();
        repaint();
    }

    int getRightEdge() const { return tabs.empty() ? 0 : tabs.back().bounds.getRight(); }

    void resized() override { layout(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ui::theme::surface);
        g.setColour (ui::theme::border);
        g.fillRect (getLocalBounds().removeFromBottom (1));

        for (const auto& tab : tabs)
        {
            auto r = tab.bounds;
            const auto isActive = tab.id == active;
            const auto hover = tab.bounds.contains (getMouseXYRelative()) && isMouseOver();

            if (isActive)
            {
                g.setColour (ui::theme::canvas);
                g.fillRect (r);
                g.setColour (ui::theme::accent);
                g.fillRect (r.removeFromBottom (2));
            }
            else if (hover)
            {
                g.setColour (ui::theme::surfaceHigh);
                g.fillRect (r);
            }

            const auto colour = isActive ? ui::theme::text : ui::theme::textMuted;
            auto content = tab.bounds.reduced (10, 0);

            if (tab.id == -1)
            {
                ui::drawIcon (g, "plus", content.toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                              owner.session.isShowLocked() ? ui::theme::textMuted.withAlpha (0.4f) : colour);
                continue;
            }

            ui::drawIcon (g, tab.id == 0 ? "workflow" : "sliders-vertical", content.removeFromLeft (14).toFloat().withSizeKeepingCentre (14.0f, 14.0f), colour);
            content.removeFromLeft (6);
            if (tab.tornOff)
                ui::drawIcon (g, "external-link", content.removeFromRight (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f), colour);

            g.setColour (colour);
            g.setFont (ui::theme::font (12.5f, isActive ? ui::theme::Weight::semiBold : ui::theme::Weight::medium));
            g.drawText (tab.name, content, juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto* tab = tabAt (e.getPosition());
        setTooltip (tab == nullptr     ? juce::String()
                    : tab->id == -1    ? juce::String ("New panel")
                    : tab->id == 0     ? juce::String ("The graph: nodes and wires (Ctrl+1)")
                    : tab->tornOff     ? "\"" + tab->name + "\" is in its own window. Click to bring it to the front."
                                       : "\"" + tab->name + "\" - right-click to rename or open in its own window");
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* tab = tabAt (e.getPosition());
        if (tab == nullptr)
            return;

        const auto id = tab->id;
        if (id == -1)
        {
            if (! owner.session.isShowLocked())
                owner.addPanel();
        }
        else if (e.mods.isPopupMenu())
        {
            if (id != 0)
                owner.showTabMenu (id);
        }
        else if (e.getNumberOfClicks() == 2 && id != 0 && ! owner.session.isShowLocked())
        {
            owner.renamePanel (id);
        }
        else
        {
            owner.showTab (id);
        }
    }

private:
    const Tab* tabAt (juce::Point<int> p) const
    {
        for (const auto& tab : tabs)
            if (tab.bounds.contains (p))
                return &tab;
        return nullptr;
    }

    void layout()
    {
        const auto font = ui::theme::font (12.5f, ui::theme::Weight::semiBold);
        auto x = 8;
        for (auto& tab : tabs)
        {
            const auto width = tab.id == -1 ? 34
                                            : 10 + 14 + 6 + juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, tab.name)) + (tab.tornOff ? 16 : 0) + 12;
            tab.bounds = { x, 0, std::min (width, 220), getHeight() - 1 };
            x += tab.bounds.getWidth();
        }
    }

    MainComponent& owner;
    std::vector<Tab> tabs;
    juce::int64 active = 0;
};

//==============================================================================
/** A panel in its own window. Closing it puts the panel back in the tabs. */
class MainComponent::PanelWindow final : public juce::DocumentWindow
{
public:
    PanelWindow (MainComponent& o, std::unique_ptr<ui::PanelView> view)
        : juce::DocumentWindow ({}, ui::theme::background, juce::DocumentWindow::allButtons), owner (o), panelId (view->getPanelId())
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (320, 240, 10000, 10000);
        setContentOwned (view.release(), false);
    }

    ui::PanelView* getView() const { return dynamic_cast<ui::PanelView*> (getContentComponent()); }

    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (&owner), id = panelId]
        {
            if (safe != nullptr)
                safe->dock (id);
        });
    }

    bool keyPressed (const juce::KeyPress& key) override { return owner.keyPressed (key); }

    juce::Rectangle<int> display;  // user area of the display it was last seen on

private:
    MainComponent& owner;
    const juce::int64 panelId;
};

MainComponent::MainComponent (AudioEngine& e, juce::PropertiesFile& s) : engine (e), settings (s)
{
    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &undoButton, &redoButton, &addButton,
                     &recordButton, &markerButton, &recordMenuButton, &muteButton, &settingsButton, &lockButton, &layoutsButton })
        addAndMakeVisible (b);

    tabs = std::make_unique<Tabs> (*this);
    addAndMakeVisible (*tabs);
    lockButton.onClick = [this]
    {
        session.setShowLocked (! session.isShowLocked());
        setModified (true);
    };
    layoutsButton.onClick = [this] { showLayoutsMenu(); };

    addAndMakeVisible (canvas);
    addAndMakeVisible (inspector);
    addAndMakeVisible (statusBar);

    newButton.onClick = [this] { newSession(); };
    openButton.onClick = [this] { openSession(); };
    saveButton.onClick = [this] { save(); };
    saveAsButton.onClick = [this] { saveAs(); };
    undoButton.onClick = [this] { session.getUndoManager().undo(); };
    redoButton.onClick = [this] { session.getUndoManager().redo(); };
    addButton.onClick = [this] { canvas.showQuickAdd (canvas.getLocalBounds().getCentre().toFloat()); };
    muteButton.onClick = [this] { engine.getCore().setOutputsMuted (! engine.getCore().areOutputsMuted()); timerCallback(); };
    settingsButton.onClick = [this] { showAudioSettings(); };
    recordButton.onClick = [this] { toggleRecording(); };
    markerButton.onClick = [this] { recording.addMarker(); };
    recordMenuButton.onClick = [this] { showRecordingMenu(); };

    recording.getSessionFile = [this] { return currentFile; };
    recording.onStateChanged = [this] { updateRecordButtons(); inspector.refreshAll(); };
    recording.onProblem = [this] (const juce::String& title, const juce::String& message) { showError (title, message); };
    inspector.isRecording = [this] { return recording.isRecording(); };

    canvas.getDeviceChannelNames = inspector.getDeviceChannelNames = [this] (bool inputs) { return deviceChannelNames (inputs); };
    inspector.onDelete = [this] { canvas.deleteSelection(); };
    inspector.getNodeLatency = [this] (graph::NodeId node) { return canvas.getNodeLatency (node); };
    inspector.getWireDelay = [this] (graph::WireId wire) { return engine.getBuilder().getWireDelay (wire); };
    inspector.onDuplicate = [this] { canvas.duplicateSelection(); };
    canvas.setMinimapVisible (settings.getBoolValue ("showMinimap", true));

    statusBar.getReportExtras = [this]
    {
        return "Session:     " + juce::String (session.getNodeIds().size()) + " nodes, "
               + juce::String (session.getState().getChildWithName (model::ids::wires).getNumChildren()) + " wires\n";
    };

    controller.onDocumentChanged = [this] { setModified (true); };
    session.getUndoManager().addChangeListener (this);
    session.getState().addListener (this);
    engine.onDeviceChanged = [this] { canvas.repaint(); };

    // Reopen the last session, or start with the default mix.
    const auto last = juce::File (settings.getValue ("lastSession"));
    if (! (last.existsAsFile() && loadFile (last, true)))
        startWithDefaultSession();

    setWantsKeyboardFocus (false);
    setSize (1280, 800);
    startTimerHz (4);
    updateButtons();
    updateRecordButtons();
    checkForInterruptedTakes();

    displayAreas = getDisplayAreas();
    syncPanels();

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe != nullptr)
        {
            safe->applyView();
            safe->canvas.fitAll();
            safe->canvas.grabKeyboardFocus();
        }
    });
}

MainComponent::~MainComponent()
{
    closeAllPanels();
    session.getState().removeListener (this);
    engine.onDeviceChanged = nullptr;
    session.getUndoManager().removeChangeListener (this);
}

void MainComponent::saveSettings()
{
    settings.setValue ("showMinimap", canvas.isMinimapVisible());
    settings.setValue ("lastSession", currentFile.getFullPathName());
}

void MainComponent::selectNodeNamed (const juce::String& name)
{
    for (auto id : session.getNodeIds())
        if (session.getNodeName (id) == name)
            selection.selectOnly (id);
}

void MainComponent::addRecorderAfter (const juce::String& name)
{
    for (auto id : session.getNodeIds())
        if (session.getNodeName (id) == name)
        {
            const auto recorder = session.addNode (nodes::types::recorder, session.getNodePosition (id) + juce::Point<float> (0.0f, 160.0f));
            session.addWire (id, 0, recorder, 0);
            setModified (false);
        }
}

void MainComponent::addDemoEffects()
{
    // For snapshots: one of each effect, fed from the first input, below everything else.
    graph::NodeId source = 0;
    auto bottom = 0.0f;
    for (auto id : session.getNodeIds())
    {
        bottom = std::max (bottom, session.getNodePosition (id).y);
        if (source == 0 && session.getNodeType (id) != nullptr && session.getNodeType (id)->id == nodes::types::hardwareInput)
            source = id;
    }

    auto x = 0.0f;
    for (auto type : { nodes::types::filter, nodes::types::eq, nodes::types::compressor, nodes::types::limiter,
                       nodes::types::gate, nodes::types::delay, nodes::types::reverb })
    {
        const auto id = session.addNode (type, { x, bottom + 320.0f });
        if (source != 0)
            session.addWire (source, 0, id, 0);
        x += 240.0f;
    }

    setModified (false);
    canvas.fitAll();
}

//==============================================================================
juce::File MainComponent::sessionsFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("StagePlotMixer").getChildFile ("Sessions");
    folder.createDirectory();
    return folder;
}

void MainComponent::startWithDefaultSession()
{
    const auto inputs = engine.getStatus().numInputs;
    closeAllPanels();
    session.clear();
    model::createDefaultSession (session, inputs > 0 ? inputs : 2);
    selection.clear();
    currentFile = juce::File();
    controller.rebuild();
    syncPanels();
    showTab (0);
    setModified (false);
}

void MainComponent::newSession()
{
    confirmDiscard ([this]
    {
        startWithDefaultSession();
        canvas.fitAll();
    });
}

void MainComponent::openSession()
{
    confirmDiscard ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open session", currentFile.existsAsFile() ? currentFile : sessionsFolder(),
                                                       "*" + fileExtension);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      loadFile (fc.getResult(), false);
                              });
    });
}

bool MainComponent::loadFile (const juce::File& file, bool quiet)
{
    closeAllPanels();
    const auto error = session.loadJson (file.loadFileAsString());

    if (error.isNotEmpty())
    {
        if (! quiet)
            showError ("Couldn't open " + file.getFileName(), error);
        return false;
    }

    selection.clear();
    currentFile = file;
    controller.rebuild();
    setModified (false);
    settings.setValue ("lastSession", file.getFullPathName());
    canvas.setScope (0);
    canvas.fitAll();
    syncPanels();
    applyViewSoon();
    return true;
}

void MainComponent::save (std::function<void (bool)> done)
{
    if (currentFile == juce::File())
    {
        saveAs (std::move (done));
        return;
    }

    const auto ok = writeTo (currentFile);
    if (done)
        done (ok);
}

void MainComponent::saveAs (std::function<void (bool)> done)
{
    const auto suggested = currentFile != juce::File() ? currentFile : sessionsFolder().getChildFile ("Untitled" + fileExtension);
    chooser = std::make_unique<juce::FileChooser> ("Save session", suggested, "*" + fileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, done] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              auto ok = false;
                              if (file != juce::File())
                                  ok = writeTo (file.withFileExtension (fileExtension));
                              if (done)
                                  done (ok);
                          });
}

bool MainComponent::writeTo (const juce::File& file)
{
    // Where the windows are is saved too, so the show comes back as it was.
    captureView();

    // Write to a temporary file first so a failed save can't damage the old one.
    juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (session.toJson()) || ! temp.overwriteTargetFileWithTemporary())
    {
        showError ("Couldn't save", "The session couldn't be written to " + file.getFullPathName()
                                        + ". Check the folder exists and isn't read-only.");
        return false;
    }

    currentFile = file;
    setModified (false);
    settings.setValue ("lastSession", file.getFullPathName());
    return true;
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (recording.isRecording())
    {
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Stop recording?",
                                                                     "A take is being recorded. Stop it first?",
                                                                     "Stop recording", "Keep recording", this);

        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this), then] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->recording.stop ([safe, then] { if (safe != nullptr) safe->confirmDiscard (then); });
        });
        return;
    }

    if (! modified)
    {
        then();
        return;
    }

    const auto name = currentFile != juce::File() ? currentFile.getFileNameWithoutExtension() : juce::String ("Untitled");
    auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Save changes?",
                                                                    "Do you want to save the changes to \"" + name + "\"?",
                                                                    "Save", "Don't Save", "Cancel", this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this), then] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
            safe->save ([then] (bool saved) { if (saved) then(); });
        else if (result == 2)
            then();
    });
}

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void MainComponent::setModified (bool shouldBeModified)
{
    if (modified != shouldBeModified)
    {
        modified = shouldBeModified;
        updateTitle();
    }
    updateButtons();
}

void MainComponent::updateTitle()
{
    const auto name = currentFile != juce::File() ? currentFile.getFileNameWithoutExtension() : juce::String ("Untitled");

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (name + (modified ? " *" : "") + juce::String::fromUTF8 (" \xe2\x80\x94 Stage Plot Mixer"));

    repaint (0, 0, getWidth(), toolbarHeight);
}

void MainComponent::updateButtons()
{
    auto& undo = session.getUndoManager();
    undoButton.setEnabled (undo.canUndo());
    redoButton.setEnabled (undo.canRedo());
    undoButton.setTooltip (undo.canUndo() ? "Undo " + undo.getUndoDescription() + " (Ctrl+Z)" : "Nothing to undo");
    redoButton.setTooltip (undo.canRedo() ? "Redo " + undo.getRedoDescription() + " (Ctrl+Shift+Z)" : "Nothing to redo");
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateButtons();
}

void MainComponent::timerCallback()
{
    checkDisplays();

    // Recording counter and disk space.
    juce::String status;
    if (recording.getState() == RecordingManager::State::recording)
    {
        const auto seconds = (int) recording.getRecordedSeconds();
        status = juce::String::formatted ("%02d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60) + "|" + recording.getTimeLeftText();
    }
    else if (recording.getState() == RecordingManager::State::idle)
    {
        status = "|" + recording.getTimeLeftText();
    }
    status << (recording.isDiskLow() ? "!" : "") << recording.getNumArmed();

    if (status != lastRecordStatus)
    {
        lastRecordStatus = status;
        repaint (recordStatusArea);
    }

    const auto muted = engine.getCore().areOutputsMuted();
    if (muted != lastMuted)
    {
        lastMuted = muted;
        muteButton.setIcon (muted ? "volume-x" : "volume-2");
        muteButton.setLabel (muted ? "Outputs muted" : "Outputs on");
        muteButton.setHighlightColour (muted ? std::optional (ui::theme::danger) : std::nullopt);
        resized();
    }
}

juce::StringArray MainComponent::deviceChannelNames (bool inputs) const
{
    juce::StringArray result;
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
        return result;

    // The engine sees only the enabled channels, in order.
    const auto names = inputs ? device->getInputChannelNames() : device->getOutputChannelNames();
    const auto active = inputs ? device->getActiveInputChannels() : device->getActiveOutputChannels();
    for (int i = 0; i < names.size(); ++i)
        if (active[i])
            result.add (names[i]);
    return result;
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager(), 0, 256, 0, 256,
                                                                           false, false, false, false);
    selector->setSize (520, 440);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = ui::theme::surface;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

//==============================================================================
void MainComponent::toggleRecording()
{
    if (recording.getState() == RecordingManager::State::idle)
    {
        if (const auto error = recording.start(); error.isNotEmpty())
            showError ("Can't record", error);
    }
    else
    {
        recording.stop();
    }
}

void MainComponent::updateRecordButtons()
{
    const auto state = recording.getState();
    const auto recordingNow = state == RecordingManager::State::recording;

    recordButton.setIcon (recordingNow ? "square" : "record");
    recordButton.setLabel (recordingNow ? "Stop" : state == RecordingManager::State::finishing ? juce::String::fromUTF8 ("Saving\xe2\x80\xa6") : "Record");
    recordButton.setTooltip (recordingNow ? "Stop recording (Ctrl+R)" : "Record every armed Recorder node (Ctrl+R)");
    recordButton.setHighlightColour (recordingNow ? std::optional (ui::theme::danger) : std::nullopt);
    recordButton.setIconColour (recordingNow ? std::nullopt : std::optional (ui::theme::danger));
    recordButton.setEnabled (state != RecordingManager::State::finishing);
    markerButton.setEnabled (recordingNow);

    lastRecordStatus = {};
    resized();
    timerCallback();
}

void MainComponent::showRecordingMenu()
{
    juce::PopupMenu preRoll;
    for (auto seconds : { 0, 10, 30, 60 })
        preRoll.addItem (seconds == 0 ? juce::String ("Off") : juce::String (seconds) + " seconds", ! recording.isRecording(),
                         recording.getPreRollSeconds() == seconds, [this, seconds] { recording.setPreRollSeconds (seconds); });

    juce::PopupMenu lowDisk;
    for (auto minutes : { 10, 30, 60, 120 })
        lowDisk.addItem ("Less than " + (minutes < 60 ? juce::String (minutes) + " min" : juce::String (minutes / 60) + " h") + " left",
                         true, recording.getLowDiskMinutes() == minutes, [this, minutes] { recording.setLowDiskMinutes (minutes); });

    juce::PopupMenu recent;
    for (const auto& path : recording.getRecentTakes())
        recent.addItem (juce::File (path).getFileName(), [path] { juce::File (path).startAsProcess(); });

    juce::PopupMenu menu;
    menu.addSectionHeader ("Recording");
    menu.addSubMenu ("Pre-roll (keep audio from before Record)", preRoll);
    menu.addSubMenu ("Warn when disk space is low", lowDisk);
    menu.addSeparator();
    menu.addItem ("Open takes folder", [this]
    {
        const auto folder = recording.getTakesFolder();
        folder.createDirectory();
        folder.startAsProcess();
    });
    menu.addSubMenu ("Recent takes", recent, recent.containsAnyActiveItems());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton));
}

void MainComponent::checkForInterruptedTakes()
{
    const auto recovered = recording.recoverInterruptedTakes();
    if (recovered.isEmpty())
        return;

    juce::StringArray names;
    for (const auto& path : recovered)
        names.add (juce::File (path).getFileName());

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this), names]
    {
        if (safe != nullptr)
            safe->showError ("Recovered a recording",
                             "The app closed unexpectedly while recording. These takes were repaired and kept, "
                             "up to the last couple of seconds before it closed:\n\n"
                                 + names.joinIntoString ("\n"));
    });
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (! mods.isCommandDown() && ! mods.isAltDown() && (key.getTextCharacter() == 'm' || key.getTextCharacter() == 'M')
        && recording.getState() == RecordingManager::State::recording)
    {
        recording.addMarker();
        return true;
    }

    if (! mods.isCommandDown())
        return false;

    const auto code = key.getKeyCode();
    auto& undo = session.getUndoManager();

    if (code == 'Z' && mods.isShiftDown()) { undo.redo(); return true; }
    if (code == 'Z')                       { undo.undo(); return true; }
    if (code == 'Y')                       { undo.redo(); return true; }
    if (code == 'S' && mods.isShiftDown()) { saveAs(); return true; }
    if (code == 'S')                       { save(); return true; }
    if (code == 'O')                       { openSession(); return true; }
    if (code == 'N')                       { newSession(); return true; }
    if (code == 'M')                       { muteButton.triggerClick(); return true; }
    if (code == 'R')                       { toggleRecording(); return true; }
    if (code == 'L' && mods.isShiftDown()) { lockButton.triggerClick(); return true; }

    // Ctrl+1: the graph; Ctrl+2...9: the panels in order.
    if (code >= '1' && code <= '9' && ! mods.isShiftDown())
    {
        const auto index = code - '1';
        if (index == 0)
        {
            showTab (0);
            return true;
        }
        const auto panels = session.getPanels();
        if (index - 1 < panels.getNumChildren())
        {
            showTab ((juce::int64) panels.getChild (index - 1)[ids::id]);
            return true;
        }
    }
    return false;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (ui::theme::background);

    auto bar = getLocalBounds().removeFromTop (toolbarHeight);
    g.setColour (ui::theme::surface);
    g.fillRect (bar);
    g.setColour (ui::theme::border);
    g.fillRect (bar.removeFromBottom (1));

    // Separators between button groups.
    for (auto* b : { &saveAsButton, &redoButton, &recordMenuButton })
        g.fillRect (b->getRight() + 8, 12, 1, toolbarHeight - 24);

    // Recording time, or the space left.
    {
        auto r = recordStatusArea;
        const auto state = recording.getState();
        const auto low = recording.isDiskLow();
        const auto left = recording.getTimeLeftText();

        if (state == RecordingManager::State::recording)
        {
            g.setColour (ui::theme::danger);
            g.fillEllipse (r.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
            r.removeFromLeft (6);
            const auto seconds = (int) recording.getRecordedSeconds();
            g.setColour (ui::theme::text);
            g.setFont (ui::theme::font (14.0f, ui::theme::Weight::semiBold, true));
            const auto time = juce::String::formatted ("%02d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
            g.drawText (time, r.removeFromLeft (70), juce::Justification::centredLeft, false);
            if (left.isNotEmpty())
            {
                g.setColour (low ? ui::theme::warning : ui::theme::textMuted);
                g.setFont (ui::theme::font (11.5f));
                g.drawText (left + " left", r, juce::Justification::centredLeft, true);
            }
        }
        else
        {
            g.setFont (ui::theme::font (11.5f));
            g.setColour (low ? ui::theme::warning : ui::theme::textMuted);
            const auto text = state == RecordingManager::State::finishing ? juce::String ("Finishing the files")
                              : recording.getNumArmed() == 0             ? juce::String ("No armed recorders")
                              : left.isNotEmpty()                        ? left + " of space"
                                                                         : juce::String();
            g.drawText (text, r, juce::Justification::centredLeft, true);
        }
    }

    // Session name, centred in the space between the groups.
    const auto name = currentFile != juce::File() ? currentFile.getFileNameWithoutExtension() : juce::String ("Untitled");
    auto area = juce::Rectangle<int>::leftTopRightBottom (addButton.getRight() + 16, 0, recordButton.getX() - 16, toolbarHeight);
    g.setFont (ui::theme::font (13.0f, ui::theme::Weight::semiBold));
    g.setColour (ui::theme::text);
    g.drawText (name + (modified ? " *" : ""), area, juce::Justification::centred, true);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (toolbarHeight).reduced (8, 7);

    auto place = [&bar] (ui::IconButton& b, bool fromRight = false)
    {
        const auto w = b.getIdealWidth (bar.getHeight());
        b.setBounds (fromRight ? bar.removeFromRight (w) : bar.removeFromLeft (w));
        if (fromRight) bar.removeFromRight (4); else bar.removeFromLeft (4);
    };

    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton })
        place (*b);
    bar.removeFromLeft (13);
    place (undoButton);
    place (redoButton);
    bar.removeFromLeft (13);
    place (addButton);

    place (settingsButton, true);
    place (muteButton, true);
    bar.removeFromRight (13);
    place (recordMenuButton, true);
    place (markerButton, true);
    recordStatusArea = bar.removeFromRight (150).withTrimmedLeft (6);
    recordStatusArea = { recordStatusArea.getX(), 0, recordStatusArea.getWidth(), toolbarHeight };
    place (recordButton, true);

    auto tabRow = area.removeFromTop (tabsHeight);
    tabs->setBounds (tabRow);
    {
        auto right = tabRow.reduced (8, 4);
        for (auto* b : { &lockButton, &layoutsButton })
        {
            const auto w = b->getIdealWidth (right.getHeight());
            b->setBounds (right.removeFromRight (w));
            right.removeFromRight (4);
        }
    }

    statusBar.setBounds (area.removeFromBottom (StatusBar::height));
    for (auto& [id, view] : dockedPanels)
        view->setBounds (area);
    inspector.setBounds (area.removeFromRight (ui::Inspector::preferredWidth));
    canvas.setBounds (area);
    lockButton.toFront (false);
    layoutsButton.toFront (false);
}

//==============================================================================
void MainComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if ((tree == session.getState() && property == ids::showLock) || tree.hasType (ids::panel))
        triggerAsyncUpdate();
}

void MainComponent::panelsChanged (const juce::ValueTree& parent)
{
    if (parent == session.getPanels() || parent == session.getState())
        triggerAsyncUpdate();
}

juce::DocumentWindow* MainComponent::getMainWindow() const
{
    return findParentComponentOfClass<juce::DocumentWindow>();
}

std::unique_ptr<ui::PanelView> MainComponent::createPanelView (juce::int64 panelId)
{
    auto view = std::make_unique<ui::PanelView> (session, canvas.getMeters(), panelId);
    view->onShowNode = [this] (graph::NodeId node) { showNodeInGraph (node); };
    view->addHostMenuItems = [this, panelId] (juce::PopupMenu& menu)
    {
        if (panelWindows.count (panelId) > 0)
            menu.addItem ("Put back in the main window", [this, panelId] { dock (panelId); });
        else
            menu.addItem ("Open in its own window", [this, panelId] { tearOff (panelId); });
    };
    return view;
}

void MainComponent::syncPanels()
{
    const auto panels = session.getPanels();
    const auto exists = [&panels] (juce::int64 id) { return panels.getChildWithProperty (ids::id, id).isValid(); };

    // Views and windows of panels that have gone.
    for (auto it = dockedPanels.begin(); it != dockedPanels.end();)
        it = exists (it->first) ? std::next (it) : dockedPanels.erase (it);
    for (auto it = panelWindows.begin(); it != panelWindows.end();)
        it = exists (it->first) ? std::next (it) : panelWindows.erase (it);

    if (currentTab != 0 && (! exists (currentTab) || panelWindows.count (currentTab) > 0))
        currentTab = 0;

    std::vector<Tabs::Tab> list { { 0, "Graph", false, {} } };
    for (const auto& panel : panels)
    {
        const auto id = (juce::int64) panel[ids::id];
        const auto name = panel[ids::name].toString();
        list.push_back ({ id, name, panelWindows.count (id) > 0, {} });

        if (auto w = panelWindows.find (id); w != panelWindows.end())
            w->second->setName (name + juce::String::fromUTF8 (" \xe2\x80\x94 Stage Plot Mixer"));
    }
    tabs->setTabs (std::move (list), currentTab);

    // Which view is showing.
    const auto onGraph = currentTab == 0;
    canvas.setVisible (onGraph);
    inspector.setVisible (onGraph);
    if (! onGraph && dockedPanels.count (currentTab) == 0)
    {
        auto view = createPanelView (currentTab);
        addChildComponent (*view);
        dockedPanels[currentTab] = std::move (view);
    }
    for (auto& [id, view] : dockedPanels)
        view->setVisible (id == currentTab);

    updateLockButton();
    addButton.setEnabled (! session.isShowLocked());
    resized();
}

void MainComponent::showTab (juce::int64 panelId)
{
    if (auto w = panelWindows.find (panelId); w != panelWindows.end())
    {
        w->second->toFront (true);
        return;
    }

    currentTab = panelId;
    syncPanels();

    if (panelId == 0)
        canvas.grabKeyboardFocus();
    else if (auto v = dockedPanels.find (panelId); v != dockedPanels.end())
        v->second->grabKeyboardFocus();
}

void MainComponent::addPanel()
{
    session.beginAction ("Add panel");
    const auto id = session.addPanel ("Panel " + juce::String (session.getPanels().getNumChildren() + 1));
    showTab (id);
}

void MainComponent::renamePanel (juce::int64 panelId)
{
    auto panel = session.findPanel (panelId);
    ui::askForText (this, "Rename panel", "Name for this panel:", panel[ids::name].toString(), "Rename",
                    [this, panel] (const juce::String& name) mutable
                    {
                        session.beginAction ("Rename panel");
                        panel.setProperty (ids::name, name, &session.getUndoManager());
                    });
}

void MainComponent::showTabMenu (juce::int64 panelId)
{
    const auto locked = session.isShowLocked();
    const auto torn = panelWindows.count (panelId) > 0;

    juce::PopupMenu menu;
    menu.addItem ("Rename...", ! locked, false, [this, panelId] { renamePanel (panelId); });
    if (torn)
        menu.addItem ("Put back in the main window", [this, panelId] { dock (panelId); });
    else
        menu.addItem ("Open in its own window", [this, panelId] { tearOff (panelId); });
    menu.addSeparator();
    menu.addItem ("Delete panel", ! locked, false, [this, panelId]
    {
        session.beginAction ("Delete panel");
        session.removePanel (panelId);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void MainComponent::tearOff (juce::int64 panelId, std::optional<juce::Rectangle<int>> bounds)
{
    if (! session.findPanel (panelId).isValid())
        return;

    if (panelWindows.count (panelId) > 0)
    {
        if (bounds)
            panelWindows[panelId]->setBounds (*bounds);
        return;
    }

    // Reuse the docked view if there is one, so its scroll position stays.
    std::unique_ptr<ui::PanelView> view;
    if (auto it = dockedPanels.find (panelId); it != dockedPanels.end())
    {
        view = std::move (it->second);
        dockedPanels.erase (it);
        removeChildComponent (view.get());
        view->setVisible (true);
    }
    else
    {
        view = createPanelView (panelId);
    }

    auto window = std::make_unique<PanelWindow> (*this, std::move (view));
    if (bounds)
    {
        window->setBounds (*bounds);
    }
    else
    {
        const auto area = getScreenBounds();
        window->setBounds (juce::Rectangle<int> (std::min (1000, area.getWidth() - 80), std::min (640, area.getHeight() - 80))
                               .withCentre (area.getCentre() + juce::Point<int> (40, 40)));
    }
    window->display = displayAreaFor (window->getBounds());
    window->setVisible (true);
    panelWindows[panelId] = std::move (window);

    if (currentTab == panelId)
        currentTab = 0;
    syncPanels();
}

void MainComponent::dock (juce::int64 panelId)
{
    if (panelWindows.erase (panelId) > 0)
    {
        currentTab = panelId;
        syncPanels();
    }
}

void MainComponent::closeAllPanels()
{
    panelWindows.clear();
    dockedPanels.clear();
    currentTab = 0;
}

void MainComponent::showNodeInGraph (graph::NodeId node)
{
    if (! session.findNode (node).isValid())
        return;

    showTab (0);
    if (auto* main = getMainWindow())
        main->toFront (true);
    canvas.setScope (session.getParent (node));
    selection.selectOnly (node);
}

//==============================================================================
void MainComponent::captureView()
{
    juce::ValueTree view (ids::view);
    view.setProperty (ids::tab, currentTab, nullptr);

    if (auto* main = getMainWindow())
    {
        view.setProperty (ids::mainBounds, main->getBounds().toString(), nullptr);
        view.setProperty (ids::mainDisplay, displayAreaFor (main->getBounds()).toString(), nullptr);
        view.setProperty (ids::maximised, main->isFullScreen(), nullptr);
    }

    // In tab order, so recalling a layout opens them in a sensible order.
    for (const auto& panel : session.getPanels())
    {
        const auto id = (juce::int64) panel[ids::id];
        if (auto w = panelWindows.find (id); w != panelWindows.end())
        {
            juce::ValueTree window (ids::window);
            window.setProperty (ids::panel, id, nullptr);
            window.setProperty (ids::bounds, w->second->getBounds().toString(), nullptr);
            window.setProperty (ids::display, displayAreaFor (w->second->getBounds()).toString(), nullptr);
            window.setProperty (ids::maximised, w->second->isFullScreen(), nullptr);
            view.appendChild (window, nullptr);
        }
    }

    const auto current = session.getView()[ids::current];
    if (! current.isVoid())
        view.setProperty (ids::current, current, nullptr);

    session.setView (view);
}

void MainComponent::applyViewSoon()
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe != nullptr)
            safe->applyView();
    });
}

void MainComponent::applyView()
{
    const auto view = session.getView();
    displayAreas = getDisplayAreas();

    const auto place = [this] (const juce::var& bounds, const juce::var& display)
    {
        return model::placeWindow (juce::Rectangle<int>::fromString (bounds.toString()),
                                   juce::Rectangle<int>::fromString (display.toString()), displayAreas);
    };

    if (auto* main = getMainWindow(); main != nullptr && view.hasProperty (ids::mainBounds))
    {
        const auto bounds = juce::Rectangle<int>::fromString (view[ids::mainBounds].toString());
        if (! bounds.isEmpty())
        {
            if (main->isFullScreen() && ! (bool) view[ids::maximised])
                main->setFullScreen (false);
            main->setBounds (place (view[ids::mainBounds], view[ids::mainDisplay]));
            if ((bool) view[ids::maximised])
                main->setFullScreen (true);
        }
        mainDisplayArea = displayAreaFor (main->getBounds());
    }

    // Windows: the ones in the view, where they were; everything else in the tabs.
    std::set<juce::int64> wanted;
    for (const auto& window : view)
    {
        const auto id = (juce::int64) window[ids::panel];
        const auto bounds = juce::Rectangle<int>::fromString (window[ids::bounds].toString());
        if (! window.hasType (ids::window) || ! session.findPanel (id).isValid() || bounds.isEmpty())
            continue;

        wanted.insert (id);
        tearOff (id, place (window[ids::bounds], window[ids::display]));
        if (auto w = panelWindows.find (id); w != panelWindows.end())
        {
            w->second->setFullScreen ((bool) window[ids::maximised]);
            w->second->display = displayAreaFor (w->second->getBounds());
        }
    }

    for (auto it = panelWindows.begin(); it != panelWindows.end();)
        it = wanted.count (it->first) > 0 ? std::next (it) : panelWindows.erase (it);

    const auto tab = (juce::int64) view.getProperty (ids::tab, 0);
    currentTab = session.findPanel (tab).isValid() && wanted.count (tab) == 0 ? tab : 0;
    syncPanels();
}

void MainComponent::checkDisplays()
{
    const auto areas = getDisplayAreas();

    if (areas == displayAreas)
    {
        // Remember which display each window is on, for when that changes.
        if (auto* main = getMainWindow())
            mainDisplayArea = displayAreaFor (main->getBounds());
        for (auto& [id, window] : panelWindows)
            window->display = displayAreaFor (window->getBounds());
        return;
    }

    // A monitor was added, removed or rearranged: keep every window on a screen.
    displayAreas = areas;

    if (auto* main = getMainWindow(); main != nullptr && ! main->isFullScreen())
        main->setBounds (model::placeWindow (main->getBounds(), mainDisplayArea, areas));
    for (auto& [id, window] : panelWindows)
        if (! window->isFullScreen())
            window->setBounds (model::placeWindow (window->getBounds(), window->display, areas));
}

void MainComponent::updateLockButton()
{
    const auto locked = session.isShowLocked();
    lockButton.setIcon (locked ? "lock" : "lock-open");
    lockButton.setLabel (locked ? "Show Lock on" : "Show Lock");
    lockButton.setHighlightColour (locked ? std::optional (ui::theme::warning) : std::nullopt);
    lockButton.setTooltip (locked ? "Show Lock is on: nothing can be moved, rewired or deleted; controls still work. Click to unlock (Ctrl+Shift+L)."
                                  : "Show Lock: stop anything being moved, rewired or deleted by accident. Controls still work (Ctrl+Shift+L).");
    canvas.repaint();
    tabs->repaint();
}

void MainComponent::showLayoutsMenu()
{
    const auto names = session.getLayoutNames();
    const auto current = session.getView()[ids::current].toString();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Layouts: where the windows and panels are");

    for (const auto& name : names)
        menu.addItem (name, true, name == current, [this, name]
        {
            if (session.recallLayout (name))
            {
                applyView();
                setModified (true);
            }
        });
    if (names.isEmpty())
        menu.addItem ("(No layouts stored yet)", false, false, [] {});

    menu.addSeparator();
    if (current.isNotEmpty() && names.contains (current))
        menu.addItem ("Update \"" + current + "\"", [this, current]
        {
            captureView();
            session.storeLayout (current);
            setModified (true);
        });

    menu.addItem ("Store current layout as...", [this, names]
    {
        ui::askForText (this, "Store layout", "Name for this layout (windows, panels and where they are):",
                        "Layout " + juce::String (names.size() + 1), "Store",
                        [this] (const juce::String& name)
                        {
                            captureView();
                            session.storeLayout (name);
                            setModified (true);
                        });
    });

    juce::PopupMenu remove;
    for (const auto& name : names)
        remove.addItem (name, [this, name]
        {
            session.removeLayout (name);
            setModified (true);
        });
    menu.addSubMenu ("Delete", remove, ! names.isEmpty());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&layoutsButton));
}

void MainComponent::addDemoPanel()
{
    auto top = session.getChildren (0);
    std::sort (top.begin(), top.end(), [this] (auto a, auto b) { return session.getNodePosition (a).x < session.getNodePosition (b).x; });
    auto bottom = 0.0f;
    for (auto id : top)
        bottom = std::max (bottom, session.getNodePosition (id).y);
    const auto strip = model::addTemplate (session, model::getTemplates ({})[0], 0, { 0.0f, bottom + 200.0f });

    const auto panelId = session.addPanel ("Mixer");
    for (auto id : top)
        if (const auto* type = session.getNodeType (id); type != nullptr && ! type->params.empty())
            ui::addFaceToPanel (session, panelId, id);

    auto group = session.addFaceGroup (session.findPanel (panelId), "Strips", { 12.0f, 400.0f });
    if (strip != 0)
        session.addFace (group, strip, {});

    session.getView().setProperty (ids::tab, panelId, nullptr);
    setModified (false);
    showTab (panelId);
}

} // namespace spm::app
