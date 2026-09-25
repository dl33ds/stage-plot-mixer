// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "app/AudioEngine.h"
#include "app/MainComponent.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace spm::app
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, AudioEngine& engine)
        : juce::DocumentWindow (name, ui::theme::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (engine), true);
        setResizable (true, true);
        setResizeLimits (800, 480, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Stage Plot Mixer"; }
    const juce::String getApplicationVersion() override { return SPM_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "StagePlotMixer";
        options.filenameSuffix = ".settings";
        options.folderName = "StagePlotMixer";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        juce::LookAndFeel::getDefaultLookAndFeel().setColour (juce::ResizableWindow::backgroundColourId, ui::theme::background);

        engine = std::make_unique<AudioEngine>();
        const auto saved = properties.getUserSettings()->getXmlValue ("audioDevice");
        const auto error = engine->initialise (saved.get());

        window = std::make_unique<MainWindow> (getApplicationName(), *engine);

        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Audio device",
                                                    "The audio device couldn't be opened:\n" + error
                                                        + "\n\nChoose another one in Audio settings.");
    }

    void shutdown() override
    {
        if (engine != nullptr)
            if (auto state = engine->createStateXml())
                properties.getUserSettings()->setValue ("audioDevice", state.get());

        properties.saveIfNeeded();
        window = nullptr;
        engine = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> window;
};

} // namespace spm::app

START_JUCE_APPLICATION (spm::app::Application)
