#include "taskbar.h"

#include <algorithm>
#include <cstdio>

namespace
{
constexpr wchar_t kClassName[] = L"NSM.Taskbar";

// The taskbar is 48 DIP tall at 100%; the readout takes 28 DIP and is centred.
constexpr int kHeightDip = 28;
constexpr int kPaddingDip = 10;
constexpr int kGapDip = 6;
constexpr int kFallbackRightDip = 220; // no TrayNotifyWnd: keep clear of the clock
constexpr int kFontPointSize = 11;

uint64_t ToU64(const FILETIME& time)
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

bool AppsUseLightTheme(){
    DWORD value = 1;
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<BYTE*>(&value), &size);
        RegCloseKey(key);
    }
    return value != 0;
}

// The overlay is topmost, so it has to get out of the way of anything that
// covers the taskbar's monitor: a full-screen video or game would otherwise
// have the readout floating on top of it.
bool FullscreenOnTaskbarMonitor(HWND self, HWND taskbar)
{
    const HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == self || foreground == taskbar) return false;

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
    if (!GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST), &info)) return false;

    const RECT& screen = info.rcMonitor;
    constexpr int kSlack = 2; // shells round their bounds by a pixel or two
    return window.left <= screen.left + kSlack && window.top <= screen.top + kSlack &&
           window.right >= screen.right - kSlack && window.bottom >= screen.bottom - kSlack;
}

// An auto-hidden taskbar slides off the monitor instead of hiding, so
// IsWindowVisible stays true; treat "not fully on its monitor" as hidden.
bool TaskbarOnScreen(HWND taskbar)
{
    RECT rect{};
    if (!GetWindowRect(taskbar, &rect)) return false;

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST), &info)) return false;

    const RECT& screen = info.rcMonitor;
    return rect.left >= screen.left && rect.top >= screen.top && rect.right <= screen.right &&
           rect.bottom <= screen.bottom;
}
} // namespace

bool TaskbarWindow::Create(HINSTANCE instance)
{
    instance_ = instance;

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = StaticProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = kClassName;
    RegisterClassExW(&cls);

    light_ = AppsUseLightTheme();
    return Dock();
}

bool TaskbarWindow::Dock()
{
    taskbar_ = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar_) return false;

    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    hwnd_ = nullptr;

    // TrafficMonitor reparents into Shell_TrayWnd (TaskBarDlg.cpp SetParent) and
    // falls back to a floating window when that fails. On Windows 11 26200 the
    // reparent succeeds but the child is never composited: the taskbar paints
    // its DirectComposition surface over it, so nothing appears on screen
    // (measured: red background never showed, while the same window as a
    // top-level overlay did). Always take the overlay path here.
    //
    // WS_EX_TRANSPARENT lets clicks fall through to the taskbar underneath;
    // WS_EX_NOACTIVATE keeps focus where it is.
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                            kClassName, L"NetSpeedMonitor", WS_POPUP | WS_CLIPSIBLINGS, 0, 0, 0,
                            0, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    dpi_ = static_cast<int>(GetDpiForWindow(taskbar_));
    if (dpi_ <= 0) dpi_ = 96;
    padding_ = MulDiv(kPaddingDip, dpi_, 96);
    ReleaseFont();
    EnsureFont(dpi_);

    SampleCpu(); // prime the baseline; the first painted value is a real delta
    return true;
}

bool TaskbarWindow::EnsureFont(int dpi)
{
    if (font_) return true;
    const int height = -MulDiv(kFontPointSize, dpi, 72);
    font_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return font_ != nullptr;
}

void TaskbarWindow::ReleaseFont()
{
    if (font_)
    {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void TaskbarWindow::Measure(const std::wstring& text)
{
    if (!hwnd_ || !font_ || !taskbar_) return;

    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, font_);
    SIZE extent{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);

    width_ = extent.cx + padding_ * 2;
    height_ = MulDiv(kHeightDip, dpi_, 96);

    RECT taskbar{};
    GetWindowRect(taskbar_, &taskbar);

    // Right edge hugs the notification area, exactly like
    // CWin11TaskbarDlg::AdjustTaskbarWndPos does with TrayNotifyWnd.
    int rightEdge = taskbar.right - MulDiv(kFallbackRightDip, dpi_, 96);
    RECT notify{};
    const HWND notifyWnd = FindWindowExW(taskbar_, nullptr, L"TrayNotifyWnd", nullptr);
    if (notifyWnd && GetWindowRect(notifyWnd, &notify))
        rightEdge = notify.left - MulDiv(kGapDip, dpi_, 96);

    const int left = rightEdge - width_;
    const int top = taskbar.top + ((taskbar.bottom - taskbar.top) - height_) / 2;

    SetWindowPos(hwnd_, HWND_TOPMOST, left, top, width_, height_,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void TaskbarWindow::Update(double downBytesPerSecond, double upBytesPerSecond)
{
    if (!hwnd_ || !IsWindow(hwnd_) || !taskbar_ || !IsWindow(taskbar_))
    {
        if (!Dock()) return;
        text_.clear();
    }

    // Hide with the taskbar (auto-hide, Win+D) and under a full-screen app.
    if (!IsWindowVisible(taskbar_) || !TaskbarOnScreen(taskbar_) ||
        FullscreenOnTaskbarMonitor(hwnd_, taskbar_))
    {
        if (IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    if (!IsWindowVisible(hwnd_))
    {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        text_.clear(); // force Measure() so the window lands on the current taskbar
    }

    const int dpi = static_cast<int>(GetDpiForWindow(taskbar_));
    if (dpi > 0 && dpi != dpi_)
    {
        dpi_ = dpi;
        padding_ = MulDiv(kPaddingDip, dpi_, 96);
        ReleaseFont();
        EnsureFont(dpi_);
        text_.clear();
    }

    const double cpu = SampleCpu();

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);

    wchar_t buffer[128];
    swprintf_s(buffer, L"\u2191%ls \u2193%ls MB/s \u00B7 \u5185\u5B58 %lu%% \u00B7 CPU %.0f%%",
               FormatMBps(upBytesPerSecond).c_str(), FormatMBps(downBytesPerSecond).c_str(),
               static_cast<unsigned long>(memory.dwMemoryLoad), cpu);

    if (text_ == buffer) return;

    text_ = buffer;
    Measure(text_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

double TaskbarWindow::SampleCpu()
{
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return cpuPercent_;

    const uint64_t idleNow = ToU64(idle);
    const uint64_t kernelNow = ToU64(kernel);
    const uint64_t userNow = ToU64(user);

    if (!cpuPrimed_)
    {
        lastIdle_ = idleNow;
        lastKernel_ = kernelNow;
        lastUser_ = userNow;
        cpuPrimed_ = true;
        return cpuPercent_;
    }

    const uint64_t idleDelta = idleNow - lastIdle_;
    // Kernel time already contains the idle time, so the denominator is
    // kernel + user, and busy time is that minus idle.
    const uint64_t totalDelta = (kernelNow - lastKernel_) + (userNow - lastUser_);
    lastIdle_ = idleNow;
    lastKernel_ = kernelNow;
    lastUser_ = userNow;

    if (totalDelta > 0)
    {
        const double usage =
            100.0 * (1.0 - static_cast<double>(idleDelta) / static_cast<double>(totalDelta));
        cpuPercent_ = std::clamp(usage, 0.0, 100.0);
    }
    return cpuPercent_;
}

LRESULT CALLBACK TaskbarWindow::StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* self = reinterpret_cast<TaskbarWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->Handle(hwnd, message, wParam, lParam);

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT TaskbarWindow::Handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);

        RECT rect{};
        GetClientRect(hwnd, &rect);

        // Double buffered: the taskbar repaints often and a direct draw flickers.
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
        HGDIOBJ oldBitmap = SelectObject(memory, bitmap);

        const COLORREF background = light_ ? RGB(243, 243, 243) : RGB(32, 32, 32);
        const COLORREF foreground = light_ ? RGB(26, 26, 26) : RGB(240, 240, 240);

        HBRUSH brush = CreateSolidBrush(background);
        FillRect(memory, &rect, brush);
        DeleteObject(brush);

        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, foreground);
        HGDIOBJ oldFont = SelectObject(memory, font_);

        RECT textRect = rect;
        textRect.left += padding_;
        DrawTextW(memory, text_.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

        SelectObject(memory, oldFont);
        BitBlt(dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);

        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        if (hwnd_ == hwnd) hwnd_ = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
