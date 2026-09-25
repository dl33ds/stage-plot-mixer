// Stage Plot Mixer — Copyright (C) 2026 Stage Plot Mixer contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "platform/HardwareInfo.h"

#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <powrprof.h>

#include <cstdio>
#include <string_view>

namespace spm::platform
{

namespace
{

std::string toUtf8 (std::wstring_view w)
{
    if (w.empty())
        return {};

    const auto len = WideCharToMultiByte (CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
    std::string out ((size_t) len, '\0');
    WideCharToMultiByte (CP_UTF8, 0, w.data(), (int) w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring readRegistryString (HKEY root, const wchar_t* path, const wchar_t* value)
{
    DWORD size = 0;
    if (RegGetValueW (root, path, value, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0)
        return {};

    std::wstring buffer (size / sizeof (wchar_t), L'\0');
    if (RegGetValueW (root, path, value, RRF_RT_REG_SZ, nullptr, buffer.data(), &size) != ERROR_SUCCESS)
        return {};

    buffer.resize (wcsnlen (buffer.c_str(), buffer.size()));
    return buffer;
}

DWORD readRegistryDword (HKEY root, const wchar_t* path, const wchar_t* value)
{
    DWORD data = 0, size = sizeof (data);
    RegGetValueW (root, path, value, RRF_RT_REG_DWORD, nullptr, &data, &size);
    return data;
}

std::string describeOs()
{
    constexpr auto key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

    auto product = readRegistryString (HKEY_LOCAL_MACHINE, key, L"ProductName");
    const auto display = readRegistryString (HKEY_LOCAL_MACHINE, key, L"DisplayVersion");
    const auto build = readRegistryString (HKEY_LOCAL_MACHINE, key, L"CurrentBuild");
    const auto ubr = readRegistryDword (HKEY_LOCAL_MACHINE, key, L"UBR");

    // Windows 11 still reports "Windows 10" in ProductName; the build number is authoritative.
    if (_wtoi (build.c_str()) >= 22000)
        if (const auto pos = product.find (L"Windows 10"); pos != std::wstring::npos)
            product.replace (pos, 10, L"Windows 11");

    auto text = toUtf8 (product);
    if (! display.empty())
        text += " " + toUtf8 (display);
    text += " (build " + toUtf8 (build) + "." + std::to_string (ubr) + ")";
    return text;
}

std::string describePowerPlan()
{
    GUID* scheme = nullptr;
    if (PowerGetActiveScheme (nullptr, &scheme) != ERROR_SUCCESS || scheme == nullptr)
        return "unknown";

    DWORD size = 0;
    PowerReadFriendlyName (nullptr, scheme, nullptr, nullptr, nullptr, &size);
    std::wstring name (size / sizeof (wchar_t) + 1, L'\0');
    const auto ok = PowerReadFriendlyName (nullptr, scheme, nullptr, nullptr, (UCHAR*) name.data(), &size) == ERROR_SUCCESS;
    LocalFree (scheme);

    if (! ok)
        return "unknown";

    name.resize (wcsnlen (name.c_str(), name.size()));
    return toUtf8 (name);
}

std::string getProperty (HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property)
{
    DWORD type = 0, size = 0;
    SetupDiGetDeviceRegistryPropertyW (set, &data, property, &type, nullptr, 0, &size);
    if (size == 0)
        return {};

    std::wstring buffer (size / sizeof (wchar_t) + 1, L'\0');
    if (! SetupDiGetDeviceRegistryPropertyW (set, &data, property, &type, (PBYTE) buffer.data(), size, nullptr))
        return {};

    // REG_MULTI_SZ values (hardware IDs): keep only the first entry.
    return toUtf8 (buffer.c_str());
}

std::string describeStatus (DEVINST devInst)
{
    ULONG status = 0, problem = 0;
    if (CM_Get_DevNode_Status (&status, &problem, devInst, 0) != CR_SUCCESS)
        return "unknown";

    if ((status & DN_HAS_PROBLEM) == 0)
        return (status & DN_STARTED) != 0 ? "OK" : "not started";

    switch (problem)
    {
        case CM_PROB_NOT_CONFIGURED:     return "problem: not configured (code 1)";
        case CM_PROB_FAILED_START:       return "problem: device cannot start (code 10)";
        case CM_PROB_FAILED_INSTALL:     return "problem: driver not installed (code 28)";
        case CM_PROB_DISABLED:           return "problem: disabled (code 22)";
        case CM_PROB_DRIVER_FAILED_LOAD: return "problem: driver failed to load (code 39)";
        default:                         return "problem: code " + std::to_string (problem);
    }
}

std::string describeChipset (const std::string& hardwareId)
{
    struct Vendor { const char* id; const char* note; };
    static constexpr Vendor vendors[] = {
        { "VEN_104C", "Texas Instruments chipset (recommended for audio)" },
        { "VEN_1106", "VIA chipset (often problematic with audio interfaces)" },
        { "VEN_11C1", "LSI/Agere chipset" },
        { "VEN_1180", "Ricoh chipset (often problematic with audio interfaces)" },
        { "VEN_197B", "JMicron chipset (often problematic with audio interfaces)" },
        { "VEN_1033", "NEC chipset" },
    };

    for (const auto& v : vendors)
        if (hardwareId.find (v.id) != std::string::npos)
            return v.note;

    return {};
}

template <typename Filter>
std::vector<DeviceEntry> enumerate (const GUID* classGuid, const wchar_t* enumerator, DWORD flags, Filter&& filter)
{
    std::vector<DeviceEntry> result;

    const auto set = SetupDiGetClassDevsW (classGuid, enumerator, nullptr, flags);
    if (set == INVALID_HANDLE_VALUE)
        return result;

    SP_DEVINFO_DATA data {};
    data.cbSize = sizeof (data);

    for (DWORD i = 0; SetupDiEnumDeviceInfo (set, i, &data); ++i)
    {
        DeviceEntry e;
        e.name = getProperty (set, data, SPDRP_FRIENDLYNAME);
        if (e.name.empty())
            e.name = getProperty (set, data, SPDRP_DEVICEDESC);
        e.manufacturer = getProperty (set, data, SPDRP_MFG);
        e.driverService = getProperty (set, data, SPDRP_SERVICE);
        e.hardwareId = getProperty (set, data, SPDRP_HARDWAREID);
        e.status = describeStatus (data.DevInst);

        if (filter (e))
            result.push_back (std::move (e));
    }

    SetupDiDestroyDeviceInfoList (set);
    return result;
}

} // namespace

HardwareInfo queryHardwareInfo()
{
    HardwareInfo info;
    info.available = true;
    info.osDescription = describeOs();
    info.powerPlan = describePowerPlan();

    const auto all = [] (const DeviceEntry&) { return true; };

    info.fireWireControllers = enumerate (&GUID_DEVCLASS_1394, nullptr, DIGCF_PRESENT, all);
    for (auto& c : info.fireWireControllers)
        c.note = describeChipset (c.hardwareId);

    // Anything enumerated by the 1394 bus driver is a device plugged into a FireWire port.
    info.fireWireDevices = enumerate (nullptr, L"1394", DIGCF_PRESENT | DIGCF_ALLCLASSES, all);

    info.soundDevices = enumerate (&GUID_DEVCLASS_MEDIA, nullptr, DIGCF_PRESENT, all);

    return info;
}

void prepareConsole()
{
    SetConsoleOutputCP (CP_UTF8);
}

} // namespace spm::platform
