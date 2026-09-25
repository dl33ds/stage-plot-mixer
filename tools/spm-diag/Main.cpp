// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// spm-diag: hardware diagnostics for Stage Plot Mixer.
// Lists audio drivers/devices/channels, checks FireWire controllers, and runs
// stability and round-trip latency tests. Everything shown is saved to a report file.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "Console.h"
#include "DeviceSurvey.h"
#include "DeviceTests.h"
#include "platform/HardwareInfo.h"

#include <iostream>
#include <map>
#include <optional>

using namespace spm::diag;

namespace
{

constexpr auto helpText = R"(spm-diag - Stage Plot Mixer hardware diagnostics

Usage:
  spm-diag                          Interactive mode (recommended)
  spm-diag --list                   List system info and audio devices, save report, exit
  spm-diag --test stability|latency --type <driver> --device <name> [options]

Options:
  --type <name>            Driver type, e.g. "ASIO", "Windows Audio", "CoreAudio"
  --device <name>          Device name (used for both input and output)
  --input-device <name>    Input device (driver types with separate in/out devices)
  --output-device <name>   Output device
  --rate <hz>              Sample rate (default 48000)
  --buffer <samples>       Buffer size (default: the device's default)
  --seconds <n>            Stability test duration (default 60)
  --in <n> --out <n>       Latency test loopback channels, 1-based (default 1 and 1)
  --level <dB>             Latency test signal level (default -12)
  --report <file>          Where to save the report
  --yes                    Don't ask questions (use defaults)
  --help, --version
)";

/** Minimal option parser: accepts "--name value" and "--name=value". */
class Options
{
public:
    Options (int argc, char* argv[])
    {
        for (int i = 1; i < argc; ++i)
        {
            const juce::String arg { juce::CharPointer_UTF8 { argv[i] } };

            if (! arg.startsWith ("--"))
            {
                unknown.add (arg);
                continue;
            }

            if (arg.contains ("="))
            {
                values[arg.upToFirstOccurrenceOf ("=", false, false)] = arg.fromFirstOccurrenceOf ("=", false, false);
            }
            else if (i + 1 < argc && ! juce::String (argv[i + 1]).startsWith ("--") && takesValue (arg))
            {
                values[arg] = juce::String (juce::CharPointer_UTF8 (argv[++i]));
            }
            else
            {
                values[arg] = {};
            }
        }
    }

    bool has (const juce::String& name) const { return values.count (name) > 0; }

    juce::String get (const juce::String& name, const juce::String& fallback = {}) const
    {
        const auto it = values.find (name);
        return it != values.end() && it->second.isNotEmpty() ? it->second : fallback;
    }

    juce::StringArray unknown;

private:
    static bool takesValue (const juce::String& name)
    {
        return name != "--list" && name != "--yes" && name != "--help" && name != "--version";
    }

    std::map<juce::String, juce::String> values;
};

juce::File defaultReportFile()
{
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("StagePlotMixer")
        .getChildFile ("Diagnostics")
        .getChildFile ("spm-diag-" + stamp + ".txt");
}

void printHeader (Console& console)
{
    console.line ("Stage Plot Mixer - hardware diagnostics (spm-diag) v" SPM_VERSION " [" SPM_GIT_HASH "]");
    console.line ("Report created " + juce::Time::getCurrentTime().toString (true, true, true, true));
    console.line ("JUCE " + juce::String (JUCE_MAJOR_VERSION) + "." + juce::String (JUCE_MINOR_VERSION) + "."
                  + juce::String (JUCE_BUILDNUMBER));
}

std::optional<int> parseIndex (const juce::String& text, int count)
{
    if (! text.containsOnly ("0123456789") || text.isEmpty())
        return std::nullopt;

    const auto n = text.getIntValue();
    if (n < 1 || n > count)
        return std::nullopt;

    return n - 1;
}

/** Interactive device picker. Pairs input and output devices for driver types that list them separately. */
std::optional<DeviceChoice> chooseDevice (Console& console, const DeviceSurvey& survey)
{
    const auto& entries = survey.getEntries();

    if (entries.isEmpty())
    {
        console.line ("  No devices available.");
        return std::nullopt;
    }

    console.line();
    for (int i = 0; i < entries.size(); ++i)
        console.line ("  [" + juce::String (i + 1) + "] " + entries.getReference (i).describe());

    const auto index = parseIndex (console.ask ("  Device number"), entries.size());
    if (! index)
    {
        console.line ("  Not a valid device number.");
        return std::nullopt;
    }

    const auto& picked = entries.getReference (*index);
    DeviceChoice choice { picked.typeName, picked.isInput ? picked.name : juce::String(),
                          picked.isOutput ? picked.name : juce::String() };

    if (! picked.separateInputsAndOutputs)
        return choice;

    // Offer a device for the other direction, from the same driver type.
    const auto wantInput = ! picked.isInput;
    juce::Array<int> candidates;
    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries.getReference (i);
        if (e.typeName == picked.typeName && (wantInput ? e.isInput : e.isOutput))
            candidates.add (i);
    }

    if (candidates.isEmpty())
        return choice;

    console.line ("  Also choose an " + juce::String (wantInput ? "input" : "output") + " device (Enter for none):");
    for (int i = 0; i < candidates.size(); ++i)
        console.line ("    [" + juce::String (i + 1) + "] " + entries.getReference (candidates[i]).name);

    if (const auto other = parseIndex (console.ask ("  Number"), candidates.size()))
    {
        const auto& name = entries.getReference (candidates[*other]).name;
        (wantInput ? choice.inputDevice : choice.outputDevice) = name;
    }

    return choice;
}

void askSettings (Console& console, TestSettings& settings, bool latency)
{
    settings.sampleRate = console.ask ("  Sample rate", juce::String ((int) settings.sampleRate)).getDoubleValue();
    settings.bufferSize = console.ask ("  Buffer size in samples (0 = device default)", juce::String (settings.bufferSize)).getIntValue();

    if (latency)
    {
        settings.outputChannel = console.ask ("  Output channel for the loopback cable", juce::String (settings.outputChannel)).getIntValue();
        settings.inputChannel = console.ask ("  Input channel for the loopback cable", juce::String (settings.inputChannel)).getIntValue();
    }
    else
    {
        settings.seconds = console.ask ("  Duration in seconds", juce::String (settings.seconds, 0)).getDoubleValue();
    }
}

void runTest (Console& console, const DeviceSurvey& survey, const DeviceChoice& choice,
              const TestSettings& settings, bool latency)
{
    auto* type = survey.findType (choice.typeName);
    if (type == nullptr)
    {
        console.line ("  Unknown driver type: " + choice.typeName);
        return;
    }

    if (latency)
        runLatencyTest (console, *type, choice, settings);
    else
        runStabilityTest (console, *type, choice, settings);
}

void runInteractive (Console& console, DeviceSurvey& survey, TestSettings settings)
{
    for (;;)
    {
        console.heading ("Menu");
        console.line ("  1  Stability & input level test (recommended first)");
        console.line ("  2  Round-trip latency test (needs a loopback cable)");
        console.line ("  3  Rescan and list devices again");
        console.line ("  4  Save report and quit");

        const auto option = console.ask ("  Choose", "4");

        if (option == "1" || option == "2")
        {
            const auto latency = option == "2";
            if (const auto choice = chooseDevice (console, survey))
            {
                askSettings (console, settings, latency);
                runTest (console, survey, *choice, settings, latency);
            }
        }
        else if (option == "3")
        {
            survey.scan();
            survey.print (console);
        }
        else if (option == "4" || option.equalsIgnoreCase ("q"))
        {
            return;
        }
    }
}

} // namespace

int main (int argc, char* argv[])
{
    spm::platform::prepareConsole();

    const Options options (argc, argv);

    if (options.has ("--help") || ! options.unknown.isEmpty())
    {
        std::cout << helpText;
        return options.unknown.isEmpty() ? 0 : 2;
    }

    if (options.has ("--version"))
    {
        std::cout << "spm-diag " SPM_VERSION " [" SPM_GIT_HASH "]\n";
        return 0;
    }

    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const auto testName = options.get ("--test");
    const auto interactive = ! options.has ("--yes") && ! options.has ("--list") && testName.isEmpty();

    Console console (interactive);

    TestSettings settings;
    settings.sampleRate = options.get ("--rate", "48000").getDoubleValue();
    settings.bufferSize = options.get ("--buffer", "0").getIntValue();
    settings.seconds = options.get ("--seconds", "60").getDoubleValue();
    settings.inputChannel = options.get ("--in", "1").getIntValue();
    settings.outputChannel = options.get ("--out", "1").getIntValue();
    settings.probeLevelDb = juce::jlimit (-40.0f, -6.0f, options.get ("--level", "-12").getFloatValue());

    printHeader (console);
    DeviceSurvey::printSystemInfo (console);

    console.line();
    console.line ("Scanning audio devices (this can take a few seconds)...");
    DeviceSurvey survey;
    survey.scan();
    survey.print (console);

    int exitCode = 0;

    if (testName.isNotEmpty())
    {
        if (testName != "stability" && testName != "latency")
        {
            console.line ("Unknown test: " + testName);
            exitCode = 2;
        }
        else
        {
            const auto device = options.get ("--device");
            const DeviceChoice choice { options.get ("--type"),
                                        options.get ("--input-device", device),
                                        options.get ("--output-device", device) };
            runTest (console, survey, choice, settings, testName == "latency");
        }
    }
    else if (interactive)
    {
        runInteractive (console, survey, settings);
    }

    const auto reportFile = options.has ("--report") ? juce::File::getCurrentWorkingDirectory().getChildFile (options.get ("--report"))
                                                     : defaultReportFile();

    console.line();
    if (console.saveReport (reportFile))
        console.line ("Report saved to: " + reportFile.getFullPathName());
    else
        console.line ("Could not save the report to: " + reportFile.getFullPathName());

    if (interactive)
    {
        std::cout << "\nPlease send the report file above. Press Enter to close." << std::flush;
        std::string ignored;
        std::getline (std::cin, ignored);
    }

    return exitCode;
}
