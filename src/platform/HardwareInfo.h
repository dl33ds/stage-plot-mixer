// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace spm::platform
{

struct DeviceEntry
{
    std::string name;
    std::string manufacturer;
    std::string driverService;  // e.g. "1394ohci" (Windows service name)
    std::string hardwareId;     // first hardware ID, e.g. "PCI\VEN_104C&DEV_8023..."
    std::string status;         // "OK", or a description of the driver problem
    std::string note;           // extra hint for the report (e.g. chipset vendor)
};

struct HardwareInfo
{
    bool available = false;     // false on platforms without an implementation
    std::string osDescription;  // e.g. "Windows 11 Pro 23H2 (build 22631.4317)"
    std::string powerPlan;      // active power scheme name
    std::vector<DeviceEntry> fireWireControllers;
    std::vector<DeviceEntry> fireWireDevices;  // devices enumerated on the 1394 bus
    std::vector<DeviceEntry> soundDevices;     // "Sound, video and game controllers"
};

/** Gathers OS and hardware details useful for diagnosing audio interface problems.
    Must not be called from the audio thread (it is slow).
*/
HardwareInfo queryHardwareInfo();

/** Configures the console for UTF-8 output where needed. */
void prepareConsole();

} // namespace spm::platform
