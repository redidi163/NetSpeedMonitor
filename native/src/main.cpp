#include "adapter.h"
#include "ball.h"
#include "settings.h"
#include "tray.h"

#include <cstdio>
#include <cstdlib>

#include <gdiplus.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace
{
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTimerId = 1;
constexpr int kTickMs = 1000;
constexpr int kRouteRecheckTicks = 5; // SPEC 14: re-read the default route every 5 s

struct App
{
    AdapterSampler sampler;
    BallWindow ball;
    TrayIcon tray;
    Settings settings;
    bool elevated = false;
    int tick = 0;
    HWND host = nullptr;
};

App* g_app = nullptr;

bool IsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);

    return ok && elevation.TokenIsElevated != 0;
}

void UpdateTooltip(App& app)
{
    std::wstring name = AdapterNameOf(app.sampler.Index());
    if (name.empty()) name = L"无接口";

    std::wstring tip = L"NetSpeedMonitor \u00B7 " + name;
    if (!app.elevated) tip += L"（受限）";
    app.tray.SetTooltip(tip);
}

void OnTick(App& app)
{
    // Auto mode re-reads the default route every 5 ticks; a pinned interface
    // stays pinned even when it disappears (SPEC 14).
    if (app.settings.adapterIndex == 0 && app.tick % kRouteRecheckTicks == 0)
    {
        app.sampler.FollowDefaultRoute();
    }

    const AdapterRate rate = app.sampler.Sample();
    app.ball.SetRates(rate.down, rate.up);

    if (app.tick % kRouteRecheckTicks == 0) UpdateTooltip(app);
    ++app.tick;
}

void ShowTrayMenu(App& app)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, 1, app.ball.IsVisible() ? L"隐藏悬浮球" : L"显示悬浮球");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"退出");

    SetForegroundWindow(app.host);

    POINT cursor{};
    GetCursorPos(&cursor);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       cursor.x, cursor.y, 0, app.host, nullptr);
    DestroyMenu(menu);

    if (command == 1) app.ball.Toggle();
    else if (command == 2) DestroyWindow(app.host);
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    App* app = g_app;
    if (!app) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message)
    {
    case WM_TIMER:
        if (wParam == kTimerId) OnTick(*app);
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
        {
            app->sampler.ResetBaseline();
        }
        return TRUE;

    case kTrayMessage:
        if (LOWORD(lParam) == WM_LBUTTONUP) app->ball.Toggle();
        else if (LOWORD(lParam) == WM_RBUTTONUP) ShowTrayMenu(*app);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int RunProbe(int seconds)
{
    AdapterSampler sampler;
    sampler.FollowDefaultRoute();
    const uint32_t index = sampler.Index();

    uint32_t lastIn = 0;
    uint32_t lastOut = 0;
    if (index == 0 || !sampler.TryReadRaw(lastIn, lastOut))
    {
        printf("# no adapter\n");
        return 1;
    }

    printf("# adapter %u\n", index);
    printf("# tick cum_down cum_up\n");
    fflush(stdout);

    uint64_t cumulativeIn = 0;
    uint64_t cumulativeOut = 0;

    for (int i = 0; i < seconds; ++i)
    {
        Sleep(1000);

        uint32_t inNow = 0;
        uint32_t outNow = 0;
        if (!sampler.TryReadRaw(inNow, outNow)) continue;

        cumulativeIn += static_cast<uint32_t>(inNow - lastIn);
        cumulativeOut += static_cast<uint32_t>(outNow - lastOut);
        lastIn = inNow;
        lastOut = outNow;

        printf("T %d %llu %llu\n", i, static_cast<unsigned long long>(cumulativeIn),
               static_cast<unsigned long long>(cumulativeOut));
        fflush(stdout);
    }

    printf("# done\n");
    fflush(stdout);
    return 0;
}
} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2 && wcscmp(argv[1], L"--probe") == 0)
    {
        const int seconds = argc >= 3 ? _wtoi(argv[2]) : 30;
        const int code = RunProbe(seconds);
        LocalFree(argv);
        Gdiplus::GdiplusShutdown(gdiToken);
        return code;
    }
    LocalFree(argv);

    // Different name from the C# build so both can run side by side.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\NetSpeedMonitor.Native");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex) CloseHandle(mutex);
        Gdiplus::GdiplusShutdown(gdiToken);
        return 0;
    }

    App app;
    g_app = &app;
    app.elevated = IsElevated();
    app.settings = LoadSettings();

    WNDCLASSEXW hostClass{};
    hostClass.cbSize = sizeof(hostClass);
    hostClass.lpfnWndProc = HostProc;
    hostClass.hInstance = instance;
    hostClass.lpszClassName = L"NSM.Host";
    RegisterClassExW(&hostClass);

    app.host = CreateWindowExW(0, L"NSM.Host", L"NetSpeedMonitor", WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!app.host)
    {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }

    if (app.settings.adapterIndex != 0) app.sampler.Select(app.settings.adapterIndex);
    else app.sampler.FollowDefaultRoute();

    if (!app.ball.Create(instance, 60, 60))
    {
        DestroyWindow(app.host);
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }

    app.tray.Create(instance, app.host, kTrayMessage);
    UpdateTooltip(app);

    SetTimer(app.host, kTimerId, kTickMs, nullptr);
    OnTick(app);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    app.tray.Remove();
    g_app = nullptr;
    CloseHandle(mutex);
    Gdiplus::GdiplusShutdown(gdiToken);
    return 0;
}
