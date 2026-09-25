// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DeviceSurvey.h"

#include "platform/HardwareInfo.h"

namespace spm::diag
{

juce::String DeviceEntry::describe() const
{
    juce::String direction;
    if (separateInputsAndOutputs)
        direction = isInput ? " (input)" : " (output)";

    return typeName + ": " + name + direction;
}

juce::String DeviceChoice::describe() const
{
    if (inputDevice == outputDevice)
        return typeName + ": " + inputDevice;

    return typeName + ": in = " + (inputDevice.isEmpty() ? "none" : inputDevice)
         + ", out = " + (outputDevice.isEmpty() ? "none" : outputDevice);
}

juce::String describeRates (const juce::Array<double>& rates)
{
    juce::StringArray parts;
    for (auto r : rates)
        parts.add (juce::String ((int) r));

    return parts.isEmpty() ? juce::String ("none reported") : parts.joinIntoString (", ");
}

juce::String describeBuffers (const juce::Array<int>& sizes, int defaultSize)
{
    if (sizes.isEmpty())
        return "none reported";

    juce::StringArray parts;
    for (auto s : sizes)
        parts.add (juce::String (s));

    return parts.joinIntoString (", ") + " (default " + juce::String (defaultSize) + ")";
}

DeviceSurvey::DeviceSurvey()
{
    juce::AudioDeviceManager manager;
    manager.createAudioDeviceTypes (types);
}

void DeviceSurvey::scan()
{
    entries.clear();

    for (auto* type : types)
    {
        type->scanForDevices();

        const auto separate = type->hasSeparateInputsAndOutputs();

        auto probe = [&] (const juce::String& name, bool asInput, bool asOutput)
        {
            DeviceEntry e;
            e.typeName = type->getTypeName();
            e.name = name;
            e.isInput = asInput;
            e.isOutput = asOutput;
            e.separateInputsAndOutputs = separate;

            std::unique_ptr<juce::AudioIODevice> device (type->createDevice (asOutput ? name : juce::String(),
                                                                             asInput ? name : juce::String()));
            if (device == nullptr)
            {
                e.error = "driver could not create the device";
            }
            else
            {
                e.inputChannels = device->getInputChannelNames();
                e.outputChannels = device->getOutputChannelNames();
                e.sampleRates = device->getAvailableSampleRates();
                e.bufferSizes = device->getAvailableBufferSizes();
                e.defaultBufferSize = device->getDefaultBufferSize();
                e.error = device->getLastError();
            }

            entries.add (e);
        };

        if (separate)
        {
            for (const auto& name : type->getDeviceNames (true))
                probe (name, true, false);

            for (const auto& name : type->getDeviceNames (false))
                probe (name, false, true);
        }
        else
        {
            for (const auto& name : type->getDeviceNames (false))
                probe (name, true, true);
        }
    }
}

juce::AudioIODeviceType* DeviceSurvey::findType (const juce::String& typeName) const
{
    for (auto* type : types)
        if (type->getTypeName().equalsIgnoreCase (typeName))
            return type;

    return nullptr;
}

const DeviceEntry* DeviceSurvey::findEntry (const juce::String& typeName, const juce::String& name, bool input) const
{
    for (const auto& e : entries)
        if (e.typeName.equalsIgnoreCase (typeName) && e.name == name && (input ? e.isInput : e.isOutput))
            return &e;

    return nullptr;
}

static void printChannels (Console& console, const juce::String& label, const juce::StringArray& names)
{
    console.line ("      " + label + " (" + juce::String (names.size()) + "):");

    if (names.isEmpty())
    {
        console.line ("        none");
        return;
    }

    // Four per line keeps 32+ channel devices readable.
    juce::String row;
    for (int i = 0; i < names.size(); ++i)
    {
        row << juce::String (i + 1).paddedLeft (' ', 3) << " " << names[i].quoted().paddedRight (' ', 20);

        if ((i + 1) % 4 == 0 || i == names.size() - 1)
        {
            console.line ("       " + row.trimEnd());
            row.clear();
        }
    }
}

void DeviceSurvey::print (Console& console) const
{
    console.heading ("Audio driver types");

    for (auto* type : types)
    {
        int count = 0;
        for (const auto& e : entries)
            if (e.typeName == type->getTypeName())
                ++count;

        console.line ("  " + type->getTypeName() + ": " + juce::String (count) + " device entr" + (count == 1 ? "y" : "ies"));
    }

    console.heading ("Audio devices");

    if (entries.isEmpty())
        console.line ("  No audio devices found.");

    for (int i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries.getReference (i);
        console.line();
        console.line ("  [" + juce::String (i + 1) + "] " + e.describe());

        if (e.error.isNotEmpty())
            console.line ("      Error: " + e.error);

        if (e.isInput)
            printChannels (console, "Inputs", e.inputChannels);

        if (e.isOutput)
            printChannels (console, "Outputs", e.outputChannels);

        console.line ("      Sample rates: " + describeRates (e.sampleRates));
        console.line ("      Buffer sizes: " + describeBuffers (e.bufferSizes, e.defaultBufferSize));

        if (! e.sampleRates.isEmpty() && ! e.sampleRates.contains (48000.0))
            console.line ("      WARNING: 48000 Hz is not listed for this device.");
    }
}

static void printDeviceList (Console& console, const juce::String& title,
                             const std::vector<platform::DeviceEntry>& devices)
{
    console.line ("  " + title + ":");

    if (devices.empty())
    {
        console.line ("    none found");
        return;
    }

    for (const auto& d : devices)
    {
        console.line ("    - " + juce::String (d.name) + "  [" + juce::String (d.status) + "]");

        if (! d.manufacturer.empty())
            console.line ("        Manufacturer: " + juce::String (d.manufacturer));
        if (! d.driverService.empty())
            console.line ("        Driver:       " + juce::String (d.driverService));
        if (! d.hardwareId.empty())
            console.line ("        Hardware ID:  " + juce::String (d.hardwareId));
        if (! d.note.empty())
            console.line ("        Note:         " + juce::String (d.note));
    }
}

void DeviceSurvey::printSystemInfo (Console& console)
{
    using juce::SystemStats;

    const auto hw = platform::queryHardwareInfo();

    console.heading ("System");
    console.line ("  OS:        " + (hw.osDescription.empty() ? SystemStats::getOperatingSystemName()
                                                               : juce::String (hw.osDescription))
                  + (SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : " (32-bit)"));
    console.line ("  CPU:       " + SystemStats::getCpuModel() + " - "
                  + juce::String (SystemStats::getNumPhysicalCpus()) + " cores / "
                  + juce::String (SystemStats::getNumCpus()) + " threads");
    console.line ("  Memory:    " + juce::String (SystemStats::getMemorySizeInMegabytes() / 1024.0, 1) + " GB");

    if (! hw.available)
        return;

    console.line ("  Power plan: " + juce::String (hw.powerPlan)
                  + (juce::String (hw.powerPlan).containsIgnoreCase ("performance") ? ""
                                                                                     : "  (tip: 'High performance' reduces audio dropouts)"));

    console.heading ("FireWire (IEEE 1394)");
    printDeviceList (console, "Host controllers", hw.fireWireControllers);
    printDeviceList (console, "Devices on the FireWire bus", hw.fireWireDevices);

    console.heading ("Windows sound devices");
    printDeviceList (console, "Sound, video and game controllers", hw.soundDevices);
}

} // namespace spm::diag
