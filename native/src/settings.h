#pragma once

#include "common.h"

struct Settings
{
    // 0 means "follow the default route"; anything else pins an interface.
    uint32_t adapterIndex = 0;
};

// Same file and shape as the C# build: %LOCALAPPDATA%\NetSpeedMonitor\settings.json
// ({"AdapterIndex":0}). Reading never throws: a missing or corrupt file means
// defaults, because losing a preference is not worth failing startup.
std::wstring SettingsPath();
Settings LoadSettings();
bool SaveSettings(const Settings& settings);
