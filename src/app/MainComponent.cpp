// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/MainComponent.h"

#include "model/Templates.h"
#include "ui/Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace spm::app
{

namespace
{
const juce::String fileExtension = ".mixproj";
}

MainComponent::MainComponent (AudioEngine& e, juce::PropertiesFile& s) : engine (e), settings (s)
{
    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &undoButton, &redoButton, &addButton,
                     &muteButton, &settingsButton })
        addAndMakeVisible (b);

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

    canvas.getDeviceChannelNames = inspector.getDeviceChannelNames = [this] (bool inputs) { return deviceChannelNames (inputs); };
    inspector.onDelete = [this] { canvas.deleteSelection(); };
    inspector.onDuplicate = [this] { canvas.duplicateSelection(); };
    canvas.setMinimapVisible (settings.getBoolValue ("showMinimap", true));

    statusBar.getReportExtras = [this]
    {
        return "Session:     " + juce::String (session.getNodeIds().size()) + " nodes, "
               + juce::String (session.getState().getChildWithName (model::ids::wires).getNumChildren()) + " wires\n";
    };

    controller.onDocumentChanged = [this] { setModified (true); };
    session.getUndoManager().addChangeListener (this);
    engine.onDeviceChanged = [this] { canvas.repaint(); };

    // Reopen the last session, or start with the default mix.
    const auto last = juce::File (settings.getValue ("lastSession"));
    if (! (last.existsAsFile() && loadFile (last, true)))
        startWithDefaultSession();

    setWantsKeyboardFocus (false);
    setSize (1280, 800);
    startTimerHz (4);
    updateButtons();

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe != nullptr)
        {
            safe->canvas.fitAll();
            safe->canvas.grabKeyboardFocus();
        }
    });
}

MainComponent::~MainComponent()
{
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
    session.clear();
    model::createDefaultSession (session, inputs > 0 ? inputs : 2);
    selection.clear();
    currentFile = juce::File();
    controller.rebuild();
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
    canvas.fitAll();
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
bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
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
    for (auto* b : { &saveAsButton, &redoButton })
        g.fillRect (b->getRight() + 8, 12, 1, toolbarHeight - 24);

    // Session name, centred in the space between the groups.
    const auto name = currentFile != juce::File() ? currentFile.getFileNameWithoutExtension() : juce::String ("Untitled");
    auto area = juce::Rectangle<int>::leftTopRightBottom (addButton.getRight() + 16, 0, muteButton.getX() - 16, toolbarHeight);
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

    statusBar.setBounds (area.removeFromBottom (StatusBar::height));
    inspector.setBounds (area.removeFromRight (ui::Inspector::preferredWidth));
    canvas.setBounds (area);
}

} // namespace spm::app
