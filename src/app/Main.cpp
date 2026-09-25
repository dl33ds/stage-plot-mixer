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
    MainWindow (const juce::String& name, AudioEngine& engine, juce::PropertiesFile& settings)
        : juce::DocumentWindow (name, ui::theme::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (engine, settings), true);
        setResizable (true, true);
        setResizeLimits (900, 560, 10000, 10000);

        if (! restoreWindowStateFromString (settings.getValue ("windowState")))
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
    }

    MainComponent* getMainComponent() { return dynamic_cast<MainComponent*> (getContentComponent()); }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Stage Plot Mixer"; }
    const juce::String getApplicationVersion() override { return SPM_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "StagePlotMixer";
        options.filenameSuffix = ".settings";
        options.folderName = "StagePlotMixer";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (options);

        ui::theme::loadFonts();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<AudioEngine>();
        const auto saved = properties.getUserSettings()->getXmlValue ("audioDevice");
        const auto error = engine->initialise (saved.get());

        window = std::make_unique<MainWindow> (getApplicationName(), *engine, *properties.getUserSettings());

        // For development: render the window to a PNG and quit ("--snapshot path.png [--add-recorder after] [--select name] [--record]").
        if (const auto args = juce::StringArray::fromTokens (commandLine, true); args.contains ("--snapshot"))
        {
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--snapshot") + 1].unquoted());
            auto* main = window->getMainComponent();
            if (main != nullptr && args.contains ("--add-recorder"))
                main->addRecorderAfter (args[args.indexOf ("--add-recorder") + 1].unquoted());
            if (main != nullptr && args.contains ("--select"))
                main->selectNodeNamed (args[args.indexOf ("--select") + 1].unquoted());
            if (main != nullptr && args.contains ("--record"))
                juce::Timer::callAfterDelay (500, [this] { if (auto* m = window->getMainComponent()) m->toggleRecording(); });
            juce::Timer::callAfterDelay (2500, [this, file]
            {
                if (auto* content = window->getContentComponent())
                {
                    const auto image = content->createComponentSnapshot (content->getLocalBounds(), true, 2.0f);
                    file.deleteFile();
                    juce::FileOutputStream out (file);
                    juce::PNGImageFormat().writeImageToStream (image, out);
                }
                quit();
            });
            return;
        }

        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Audio device",
                                                    "The audio device couldn't be opened:\n" + error
                                                        + "\n\nChoose another one in Audio settings.");
    }

    void shutdown() override
    {
        if (window != nullptr)
        {
            if (auto* main = window->getMainComponent())
                main->saveSettings();
            properties.getUserSettings()->setValue ("windowState", window->getWindowStateAsString());
        }

        if (engine != nullptr)
            if (auto state = engine->createStateXml())
                properties.getUserSettings()->setValue ("audioDevice", state.get());

        properties.saveIfNeeded();
        window = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        ui::theme::unloadFonts();
    }

    void systemRequestedQuit() override
    {
        // Offer to save first; quitting waits for the answer.
        if (window != nullptr)
            if (auto* main = window->getMainComponent())
            {
                main->confirmDiscard ([] { juce::JUCEApplication::getInstance()->quit(); });
                return;
            }

        quit();
    }

private:
    ui::theme::LookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> window;
};

} // namespace spm::app

START_JUCE_APPLICATION (spm::app::Application)
