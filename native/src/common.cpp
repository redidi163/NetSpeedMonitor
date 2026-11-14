#include "common.h"

#include <cstdio>

std::wstring FormatKBps(double bytesPerSecond)
{
    if (bytesPerSecond < 0) bytesPerSecond = 0;

    wchar_t buffer[32];
    swprintf_s(buffer, L"%.1f", bytesPerSecond / 1024.0);
    return buffer;
}

bool FullscreenOnMonitor(HWND reference)
{
    if (!reference) return false;

    const HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == reference) return false;

    // Our own windows are topmost overlays, never the thing to defer to.
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid == GetCurrentProcessId()) return false;

    wchar_t className[64]{};
    GetClassNameW(foreground, className, 64);
    if (_wcsicmp(className, L"Progman") == 0 || _wcsicmp(className, L"WorkerW") == 0 ||
        _wcsicmp(className, L"Shell_TrayWnd") == 0 ||
        _wcsicmp(className, L"Shell_SecondaryTrayWnd") == 0)
        return false;

    RECT window{};
    if (!GetWindowRect(foreground, &window)) return false;

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(reference, MONITOR_DEFAULTTONEAREST), &info)) return false;

    const RECT& screen = info.rcMonitor;
    constexpr int kSlack = 2; // shells round their bounds by a pixel or two
    return window.left <= screen.left + kSlack && window.top <= screen.top + kSlack &&
           window.right >= screen.right - kSlack && window.bottom >= screen.bottom - kSlack;
}
