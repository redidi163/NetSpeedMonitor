#include "adapter.h"
#include "aggregate.h"
#include "ball.h"
#include "battery.h"
#include "etw.h"
#include "panel.h"
#include "taskbar.h"
#include "tray.h"
#include "usage.h"
#include "usage_store.h"
#include "usage_window.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

#include <gdiplus.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace
{
constexpr UINT kTrayMessage = WM_APP + 1;
// Posted by the panel's usage line; opens the ranking window (native-13).
constexpr UINT kUsageMessage = WM_APP + 6;
constexpr UINT_PTR kTimerId = 1;
constexpr int kTickMs = 1000;
constexpr int kRouteRecheckTicks = 5; // SPEC 14: re-read the default route every 5 s
constexpr int kTrendPoints = 60;      // SPEC 6: 60 second mini trend
constexpr wchar_t kEtwSession[] = L"NetSpeedMonitorNative";

struct App
{
    AdapterSampler sampler;
    EtwSampler etw;
    ProcessAggregator aggregator;
    BallWindow ball;
    PanelWindow panel;
    TaskbarWindow taskbar;
    TrayIcon tray;
    UsageLedger usage;
    UsageStore store;
    UsageWindow usageWindow;
    BatteryMonitor battery;
    std::deque<float> trend;
    bool offline = false;
    int64_t lastResumeTicks = 0;
    int tick = 0;
    int forcedPercent = -1; // --battery-force=NN[c], acceptance switch
    bool forcedCharging = false;
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

std::wstring AdapterLabel(const App& app)
{
    std::wstring name = AdapterNameOf(app.sampler.Index());
    return name.empty() ? L"无接口" : name;
}

void UpdateTooltip(App& app)
{
    std::wstring tip = L"NetSpeedMonitor \u00B7 " + AdapterLabel(app);
    app.tray.SetTooltip(tip);
}

void TogglePanel(App& app)
{

    if (app.panel.IsVisible())
    {
        app.panel.Show(false);
        return;
    }

    RECT ball{};
    if (app.ball.Hwnd()) GetWindowRect(app.ball.Hwnd(), &ball);

    const int panelDpi = static_cast<int>(GetDpiForWindow(app.ball.Hwnd()));
    app.panel.SetDpi(panelDpi);

    const int panelWidth = MulDiv(280, panelDpi, 96);
    const int panelHeight = MulDiv(348, panelDpi, 96);

    int x = ball.right + 8;
    int y = ball.top;

    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(MonitorFromWindow(app.ball.Hwnd(), MONITOR_DEFAULTTONEAREST), &info))
    {
        x = std::min(x, static_cast<int>(info.rcWork.right) - panelWidth);
        x = std::max(x, static_cast<int>(info.rcWork.left));
        y = std::min(y, static_cast<int>(info.rcWork.bottom) - panelHeight);
        y = std::max(y, static_cast<int>(info.rcWork.top));
    }

    app.panel.MoveTo(x, y);
    app.panel.Show(true);
}

// Today comes from the live ledger (it already carries today's seeded rows), the
// week from persisted days plus that ledger - the store never reads today back
// out of the file, so the two cannot disagree.
void RefreshUsageWindow(App& app)
{
    const std::wstring today = LocalDateString();
    app.usageWindow.SetData(app.store.Aggregate(1, today, app.usage),
                            app.store.Aggregate(7, today, app.usage),
                            app.store.Error().empty());
}

void ShowUsageWindow(App& app)
{
    if (app.usageWindow.IsVisible())
    {
        app.usageWindow.Show(false);
        return;
    }

    RefreshUsageWindow(app);

    // Opens to the left of the panel when it is up, otherwise beside the ball.
    HWND reference = app.panel.IsVisible() ? app.panel.Hwnd() : app.ball.Hwnd();
    RECT anchor{};
    if (reference) GetWindowRect(reference, &anchor);

    const int dpi = reference ? static_cast<int>(GetDpiForWindow(reference)) : 0;
    if (dpi > 0) app.usageWindow.SetDpi(dpi);

    const SIZE size = app.usageWindow.Size();

    int x = anchor.left - size.cx - 8;
    int y = anchor.top;
    if (!app.panel.IsVisible())
    {
        x = anchor.right + 8;
        y = anchor.top;
    }

    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(MonitorFromWindow(reference, MONITOR_DEFAULTTONEAREST), &info))
    {
        x = std::min(x, static_cast<int>(info.rcWork.right) - static_cast<int>(size.cx));
        x = std::max(x, static_cast<int>(info.rcWork.left));
        y = std::min(y, static_cast<int>(info.rcWork.bottom) - static_cast<int>(size.cy));
        y = std::max(y, static_cast<int>(info.rcWork.top));
    }

    app.usageWindow.MoveTo(x, y);
    app.usageWindow.Show(true);
}

void OnTick(App& app)
{
    // SPEC 4/14: the counters always follow the default route; re-read it every
    // 5 s. There is no pinned interface, so this is the only place it changes.
    if (app.tick % kRouteRecheckTicks == 0) app.sampler.FollowDefaultRoute();

    // SPEC 14: no default route means offline. Reset the window on both edges so
    // the first online frame does not inherit the offline gap.
    const bool offline = app.sampler.Index() == 0;
    if (offline != app.offline)
    {
        app.offline = offline;
        app.aggregator.Reset();
    }

    const AdapterRate rate = offline ? AdapterRate{} : app.sampler.Sample();
    app.ball.SetRates(rate.down, rate.up);
    app.ball.UpdateFullscreen(FullscreenOnMonitor(app.ball.Hwnd()));

    BatterySnapshot battery;
    if (app.forcedPercent >= 0)
    {
        battery.present = true;
        battery.valid = true;
        battery.percent = app.forcedPercent;
        battery.charging = app.forcedCharging;
    }
    else
    {
        battery = app.battery.Snapshot();
    }
    app.taskbar.SetBattery(battery);
    app.taskbar.Update(rate.down, rate.up);

    std::vector<ProcessRow> rows;
    if (!offline && app.etw.Running())
    {
        double external = 0;
        rows = app.aggregator.Update(app.etw.Snapshot(), TicksToSeconds(NowTicks()), external);
        // The ledger outlives every window and session rebuild: it only ever
        // adds, so wake-ups and process exits cannot make a total go backwards.
        app.usage.Add(app.aggregator.TakeUsageDeltas());
    }

    app.store.MaybeSave(LocalDateString(), app.usage, TicksToSeconds(NowTicks()), false);

    app.panel.SetUsage(app.usage.TotalDown(), app.usage.TotalUp(), app.store.Error().empty());
    if (app.usageWindow.IsVisible()) RefreshUsageWindow(app);

    app.trend.push_back(static_cast<float>(rate.down / 1024.0));
    while (static_cast<int>(app.trend.size()) > kTrendPoints) app.trend.pop_front();

    if (app.panel.IsVisible())
    {
        std::wstring footer;
        if (offline) footer = L"断网 · 等待默认路由恢复";
        else if (!app.etw.Running()) footer = L"ETW 未启动：" + app.etw.Error();
        else footer = L"ETW · " + app.etw.EnableMode() + L" · " +
                      std::to_wstring(app.etw.EventCount()) + L" 事件";

        app.panel.SetData(AdapterLabel(app), rate.down, rate.up,
                          std::vector<float>(app.trend.begin(), app.trend.end()), rows, footer);
    }

    if (app.tick % kRouteRecheckTicks == 0) UpdateTooltip(app);
    ++app.tick;
}

void ShowTrayMenu(App& app)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, 1, app.ball.IsVisible() ? L"隐藏悬浮球" : L"显示悬浮球");
    AppendMenuW(menu, MF_STRING, 2, app.panel.IsVisible() ? L"隐藏面板" : L"显示面板");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"退出");

    SetForegroundWindow(app.host);

    POINT cursor{};
    GetCursorPos(&cursor);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0,
                                       app.host, nullptr);
    DestroyMenu(menu);

    if (command == 1) app.ball.Toggle();
    else if (command == 2) TogglePanel(app);
    else if (command == 3) DestroyWindow(app.host);
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
            // SPEC 14: the kernel session does not survive sleep, so rebuild it.
            // Repeated resume messages within 3 s are one wake-up.
            const int64_t now = NowTicks();
            if (app->lastResumeTicks == 0 || TicksToSeconds(now - app->lastResumeTicks) > 3.0)
            {
                app->lastResumeTicks = now;
                app->etw.Stop();
                app->etw.Start(kEtwSession);
                app->sampler.ResetBaseline();
                app->aggregator.Reset();
            }
        }
        return TRUE;

    case WM_APP + 2:
        TogglePanel(*app);
        return 0;

    case WM_APP + 3:
        // Panel finished a drag: the ball sits on its top-left corner.
        app->ball.MoveTo(static_cast<int>(wParam), static_cast<int>(lParam));
        return 0;

    case kUsageMessage:
        ShowUsageWindow(*app);
        return 0;

    case kTrayMessage:
        if (LOWORD(lParam) == WM_LBUTTONUP) app->ball.Toggle();
        else if (LOWORD(lParam) == WM_RBUTTONUP) ShowTrayMenu(*app);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        // Force the last 30 s of usage to disk before the process disappears.
        app->store.MaybeSave(LocalDateString(), app->usage, 0, true);
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

// Headless ETW reconcile: cumulative external bytes per tick plus the final
// per-PID table, so the numbers can be checked against Get-NetAdapterStatistics
// and against a known payload size.
void Emit(FILE* log, const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    vfwprintf(stdout, format, args);
    va_end(args);
    fflush(stdout);

    if (!log) return;

    va_start(args, format);
    vfwprintf(log, format, args);
    va_end(args);
    fflush(log);
}

int RunEtwProbe(int seconds, const std::wstring& path)
{
    FILE* log = nullptr;
    if (!path.empty()) _wfopen_s(&log, path.c_str(), L"w");

    EtwSampler sampler;
    if (!sampler.Start(kEtwSession))
    {
        Emit(log, L"# etw failed: %ls\n", sampler.Error().c_str());
        return 1;
    }

    ProcessAggregator aggregator;
    Emit(log, L"# etw %ls\n", sampler.EnableMode().c_str());
    Emit(log, L"# tick ext_in ext_out events\n");

    int64_t lastIn = 0;
    int64_t lastOut = 0;
    int64_t cumulativeIn = 0;
    int64_t cumulativeOut = 0;

    for (int i = 0; i < seconds; ++i)
    {
        Sleep(1000);

        int64_t inNow = 0;
        int64_t outNow = 0;
        for (const PidSnapshot& row : sampler.Snapshot())
        {
            inNow += row.counters.externalIn;
            outNow += row.counters.externalOut;
        }

        // PID reuse resets a counter to zero; without clamping, the negative
        // delta eats previously counted bytes.
        if (inNow >= lastIn) cumulativeIn += inNow - lastIn;
        if (outNow >= lastOut) cumulativeOut += outNow - lastOut;
        lastIn = inNow;
        lastOut = outNow;

        double external = 0;
        const std::vector<ProcessRow> rows =
            aggregator.Update(sampler.Snapshot(), TicksToSeconds(NowTicks()), external);

        Emit(log, L"T %d %lld %lld %lld\n", i, static_cast<long long>(cumulativeIn),
             static_cast<long long>(cumulativeOut), static_cast<long long>(sampler.EventCount()));
        for (const ProcessRow& row : rows)
        {
            Emit(log, L"  R %ls %ls %ls\n", row.name.c_str(), row.downText.c_str(),
                 row.upText.c_str());
        }
    }

    sampler.Stop(); // reads EventsLost from the stopped session
    Emit(log, L"# all=%lld\n", static_cast<long long>(sampler.AllEvents()));
    // Per-PID totals so a single process (e.g. a curl download) can be checked
    // against the byte count it reports itself.
    for (const PidSnapshot& row : sampler.Snapshot())
    {
        Emit(log, L"# pid %u ext_in %lld ext_out %lld lb_in %lld lb_out %lld\n", row.pid,
             static_cast<long long>(row.counters.externalIn),
             static_cast<long long>(row.counters.externalOut),
             static_cast<long long>(row.counters.loopbackIn),
             static_cast<long long>(row.counters.loopbackOut));
    }
    Emit(log, L"# done events=%lld lost=%lld tcp_ev=%lld udp_ev=%lld tcp_in=%lld udp_in=%lld\n",
         static_cast<long long>(sampler.EventCount()), static_cast<long long>(sampler.EventsLost()),
         static_cast<long long>(sampler.TcpEvents()), static_cast<long long>(sampler.UdpEvents()),
         static_cast<long long>(sampler.TcpExternalIn()),
         static_cast<long long>(sampler.UdpExternalIn()));
    for (int id = 0; id < 64; ++id)
    {
        if (sampler.IdCount(id) == 0) continue;
        Emit(log, L"# id %d = %lld\n", id, static_cast<long long>(sampler.IdCount(id)));
    }
    if (log) fclose(log);
    return 0;
}

int RunEtwDump(int seconds, const std::wstring& path)
{
    EtwSampler::SetDump(path, 24);

    EtwSampler sampler;
    const bool started = sampler.Start(kEtwSession);

    {
        FILE* out = nullptr;
        _wfopen_s(&out, path.c_str(), L"a");
        if (out)
        {
            fwprintf(out, L"# started=%d mode=%ls error=%ls\n", started ? 1 : 0,
                     sampler.EnableMode().c_str(), sampler.Error().c_str());
            fclose(out);
        }
    }

    if (!started)
    {
        printf("# etw failed: %ls\n", sampler.Error().c_str());
        return 1;
    }

    Sleep(seconds * 1000);

    // Elevated runs cannot redirect stdout, so the summary goes next to the
    // dump file as well.
    FILE* out = nullptr;
    _wfopen_s(&out, path.c_str(), L"a");
    if (out)
    {
        fwprintf(out, L"# etw %ls events=%lld\n", sampler.EnableMode().c_str(),
                 static_cast<long long>(sampler.EventCount()));
        for (const PidSnapshot& row : sampler.Snapshot())
        {
            fwprintf(out, L"P %u lb_in=%lld lb_out=%lld ext_in=%lld ext_out=%lld\n", row.pid,
                     static_cast<long long>(row.counters.loopbackIn),
                     static_cast<long long>(row.counters.loopbackOut),
                     static_cast<long long>(row.counters.externalIn),
                     static_cast<long long>(row.counters.externalOut));
        }
        fclose(out);
    }

    sampler.Stop();
    return 0;
}

// Cumulative per-exe totals over a fixed window, written to `path` because an
// elevated process cannot redirect stdout. Feeds native-11's monotonic and
// conservation checks, and reconciles against --etw-probe's external total.
int RunUsageProbe(int seconds, const std::wstring& path)
{
    FILE* log = nullptr;
    if (!path.empty()) _wfopen_s(&log, path.c_str(), L"w");

    EtwSampler sampler;
    if (!sampler.Start(kEtwSession))
    {
        Emit(log, L"# etw failed: %ls\n", sampler.Error().c_str());
        if (log) fclose(log);
        return 1;
    }

    ProcessAggregator aggregator;
    UsageLedger ledger;
    Emit(log, L"# usage seconds=%d mode=%ls\n", seconds, sampler.EnableMode().c_str());

    for (int i = 1; i <= seconds; ++i)
    {
        Sleep(1000);

        double external = 0;
        aggregator.Update(sampler.Snapshot(), TicksToSeconds(NowTicks()), external);
        ledger.Add(aggregator.TakeUsageDeltas());

        Emit(log, L"T %d %lld %lld %llu\n", i, static_cast<long long>(ledger.TotalDown()),
             static_cast<long long>(ledger.TotalUp()),
             static_cast<unsigned long long>(ledger.Count()));
    }

    sampler.Stop();

    for (const UsageRow& row : ledger.Top(32))
    {
        Emit(log, L"U %ls %lld %lld\n", row.name.c_str(), static_cast<long long>(row.down),
             static_cast<long long>(row.up));
    }

    Emit(log, L"# total %lld %lld accounts=%llu events=%lld lost=%lld\n",
         static_cast<long long>(ledger.TotalDown()), static_cast<long long>(ledger.TotalUp()),
         static_cast<unsigned long long>(ledger.Count()),
         static_cast<long long>(sampler.EventCount()),
         static_cast<long long>(sampler.EventsLost()));

    if (log) fclose(log);
    return 0;
}

// Receiver battery, one line per sample: the wire trace goes to `path` because an
// elevated process cannot redirect stdout. Exit code 0 only when at least one
// sample carried a 1..100 percentage, so the acceptance script can gate on it.
int RunBatteryProbe(int samples, const std::wstring& path)
{
    FILE* log = nullptr;
    if (!path.empty()) _wfopen_s(&log, path.c_str(), L"w");

    BatteryReader reader;
    const bool present = reader.DevicePresent();
    Emit(log, L"# battery present=%d\n", present ? 1 : 0);
    reader.Describe(log);

    int hits = 0;
    for (int i = 0; i < samples; ++i)
    {
        const std::optional<BatteryReading> reading = reader.Read(2500, log);
        if (reading)
        {
            ++hits;
            Emit(log, L"T %d pct=%d chg=%d proto=%ls out=%d\n", i, reading->percent,
                 reading->charging ? 1 : 0, reading->protocol.c_str(), reading->outLen);
        }
        else
        {
            Emit(log, L"T %d pct=-- err=%ls\n", i, reader.LastError().c_str());
        }
        Sleep(2000);
    }

    Emit(log, L"# done hits=%d samples=%d\n", hits, samples);
    if (log) fclose(log);
    return hits > 0 ? 0 : 1;
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

    if (argc >= 2 && wcscmp(argv[1], L"--etw-probe") == 0)
    {
        const int seconds = argc >= 3 ? _wtoi(argv[2]) : 30;
        const std::wstring path = argc >= 4 ? argv[3] : L"";
        const int code = RunEtwProbe(seconds, path);
        LocalFree(argv);
        Gdiplus::GdiplusShutdown(gdiToken);
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--etw-dump") == 0)
    {
        const int seconds = argc >= 3 ? _wtoi(argv[2]) : 10;
        const std::wstring path = argc >= 4 ? argv[3] : L"etw-dump.txt";
        const int code = RunEtwDump(seconds, path);
        LocalFree(argv);
        Gdiplus::GdiplusShutdown(gdiToken);
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--usage-probe") == 0)
    {
        const int seconds = argc >= 3 ? _wtoi(argv[2]) : 30;
        const std::wstring path = argc >= 4 ? argv[3] : L"usage-probe.txt";
        const int code = RunUsageProbe(seconds, path);
        LocalFree(argv);
        Gdiplus::GdiplusShutdown(gdiToken);
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--battery-probe") == 0)
    {
        const int samples = argc >= 3 ? _wtoi(argv[2]) : 10;
        const std::wstring path = argc >= 4 ? argv[3] : L"";
        const int code = RunBatteryProbe(samples, path);
        LocalFree(argv);
        Gdiplus::GdiplusShutdown(gdiToken);
        return code;
    }

    LocalFree(argv);

    // Acceptance switch (native-16): drive the pill without a live reading so
    // colour and blink can be checked with screenshots. Not documented in README.
    int forcedPercent = -1;
    bool forcedCharging = false;
    if (const wchar_t* force = wcsstr(GetCommandLineW(), L"--battery-force="))
    {
        forcedPercent = _wtoi(force + 16);
        for (const wchar_t* scan = force + 16; *scan && *scan != L' ' && *scan != L'"'; ++scan)
            if (*scan == L'c') forcedCharging = true;
    }

    // SPEC 3: elevated only. The manifest already forces a UAC prompt, so this
    // guard only fires when the token is stripped some other way (e.g. a
    // launcher that recreates the process unelevated).
    if (!IsElevated())
    {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 2;
    }

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

    WNDCLASSEXW hostClass{};
    hostClass.cbSize = sizeof(hostClass);
    hostClass.lpfnWndProc = HostProc;
    hostClass.hInstance = instance;
    hostClass.lpszClassName = L"NSM.Host";
    RegisterClassExW(&hostClass);

    app.host = CreateWindowExW(0, L"NSM.Host", L"NetSpeedMonitor", WS_OVERLAPPED, 0, 0, 0, 0,
                               nullptr, nullptr, instance, nullptr);
    if (!app.host)
    {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }

    app.sampler.FollowDefaultRoute();

    app.etw.Start(kEtwSession);

    if (!app.ball.Create(instance, 60, 60, app.host))
    {
        DestroyWindow(app.host);
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }

    app.panel.Create(instance, 200, 60);
    app.panel.SetNotify(app.host, WM_APP + 3);
    app.panel.SetUsageNotify(app.host, kUsageMessage);
    app.usageWindow.Create(instance);

    app.taskbar.Create(instance);

    app.forcedPercent = forcedPercent;
    app.forcedCharging = forcedCharging;
    app.battery.Start();

    app.tray.Create(instance, app.host, kTrayMessage);
    UpdateTooltip(app);

    // Seed before the first tick: a restart continues today's totals instead of
    // starting from zero.
    app.store.Open(DefaultUsageDir(), LocalDateString());
    app.store.SeedToday(LocalDateString(), app.usage);

    SetTimer(app.host, kTimerId, kTickMs, nullptr);
    OnTick(app);

    // Diagnostic: open the panel at startup so it can be captured without
    // synthesising a click (an elevated window ignores injected input).
    if (wcsstr(GetCommandLineW(), L"--panel") != nullptr) TogglePanel(app);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    app.etw.Stop();
    app.battery.Stop();
    app.tray.Remove();
    g_app = nullptr;
    CloseHandle(mutex);
    Gdiplus::GdiplusShutdown(gdiToken);
    return 0;
}
