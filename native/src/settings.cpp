#include "settings.h"

#include <shlobj.h>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace
{
std::string ReadAll(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024)
    {
        CloseHandle(file);
        return {};
    }

    std::string content(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr);
    CloseHandle(file);

    if (!ok) return {};
    content.resize(read);
    return content;
}
} // namespace

std::wstring SettingsPath()
{
    wchar_t localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"settings.json";

    std::wstring path(localAppData, length);
    path += L"\\NetSpeedMonitor\\settings.json";
    return path;
}

Settings LoadSettings()
{
    Settings settings;

    const std::string content = ReadAll(SettingsPath());
    if (content.empty()) return settings;

    const size_t key = content.find("\"AdapterIndex\"");
    if (key == std::string::npos) return settings;

    const size_t colon = content.find(':', key);
    if (colon == std::string::npos) return settings;

    size_t start = colon + 1;
    while (start < content.size() && (content[start] == ' ' || content[start] == '\t')) ++start;

    unsigned long value = 0;
    size_t end = start;
    while (end < content.size() && content[end] >= '0' && content[end] <= '9')
    {
        value = value * 10 + static_cast<unsigned long>(content[end] - '0');
        if (value > 0xFFFFFFFFul) return settings;
        ++end;
    }

    if (end == start) return settings;
    settings.adapterIndex = static_cast<uint32_t>(value);
    return settings;
}

bool SaveSettings(const Settings& settings)
{
    const std::wstring path = SettingsPath();
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;

    const std::wstring directory = path.substr(0, slash);
    SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);

    const std::string content = "{\"AdapterIndex\":" + std::to_string(settings.adapterIndex) + "}";

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);

    return ok && written == content.size();
}
