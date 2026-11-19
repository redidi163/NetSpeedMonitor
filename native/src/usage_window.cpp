#include "usage_window.h"

#include <gdiplus.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shcore.lib")

using namespace Gdiplus;

namespace
{
constexpr int kWidthDip = 320;
constexpr int kHeightDip = 420;
constexpr int kPadding = 16;
constexpr int kTitleTop = 12;
constexpr int kTabTop = 44;
constexpr int kTabHeight = 22;
constexpr int kTabWidth = 72;
constexpr int kRowsTop = 78;
constexpr int kRowHeight = 30;
constexpr int kRows = 10;
constexpr int kFooterTop = 392;
constexpr int kDragThresholdDip = 6;
constexpr UINT_PTR kForegroundTimerId = 1;
constexpr UINT kForegroundPollMs = 200;
constexpr UINT kCollapseMessage = WM_APP + 5;

const Color kBackground(0xE6, 0x14, 0x16, 0x1A);
const Color kBorder(0x2A, 0xFF, 0xFF, 0xFF);
const Color kPrimary(0xFF, 0xE8, 0xEA, 0xED);
const Color kSecondary(0xFF, 0x8A, 0x8F, 0x98);
const Color kDown(0xFF, 0x4C, 0xC2, 0xFF);
const Color kUp(0xFF, 0xFF, 0x9E, 0x6D);
const Color kBar(0x1F, 0x4C, 0xC2, 0xFF);

void AddRoundRect(GraphicsPath& path, const RectF& rect, REAL radius)
{
    const REAL d = radius * 2;
    path.AddArc(rect.X, rect.Y, d, d, 180, 90);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRounded(Graphics& graphics, const RectF& rect, REAL radius, const Color& color)
{
    GraphicsPath path;
    AddRoundRect(path, rect, radius);
    SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}

void DrawText(Graphics& graphics, const std::wstring& text, Font& font, const Color& color,
              const RectF& box, StringAlignment align = StringAlignmentNear,
              StringAlignment line = StringAlignmentCenter)
{
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(line);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);

    SolidBrush brush(color);
    graphics.DrawString(text.c_str(), -1, &font, box, &format, &brush);
}

UINT DpiForPoint(int x, int y)
{
    POINT point{x, y};
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) return dpiX;
    return 96;
}
} // namespace

bool UsageWindow::Create(HINSTANCE instance)
{
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NSM.Usage";
    RegisterClassExW(&wc);

    const int x = 200;
    const int y = 60;
    dpi_ = static_cast<int>(DpiForPoint(x, y));

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                            L"NSM.Usage", L"NetSpeedMonitor", WS_POPUP, x, y,
                            MulDiv(kWidthDip, dpi_, 96), MulDiv(kHeightDip, dpi_, 96), nullptr,
                            nullptr, instance, this);
    if (!hwnd_) return false;

    // A window that has never been shown reports 96 dpi even on a 144 dpi desktop.
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    dpi_ = static_cast<int>(GetDpiForWindow(hwnd_));
    if (dpi_ <= 0) dpi_ = 96;
    ShowWindow(hwnd_, SW_HIDE);

    ApplyDpi();
    Render();
    return true;
}

void UsageWindow::SetDpi(int dpi)
{
    if (dpi <= 0 || dpi == dpi_) return;
    dpi_ = dpi;
    ApplyDpi();
    if (visible_) Render();
}

void UsageWindow::ApplyDpi()
{
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, MulDiv(kWidthDip, dpi_, 96),
                 MulDiv(kHeightDip, dpi_, 96), SWP_NOMOVE | SWP_NOACTIVATE);
}

void UsageWindow::MoveTo(int x, int y)
{
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

POINT UsageWindow::Position() const
{
    RECT rect{};
    if (hwnd_) GetWindowRect(hwnd_, &rect);
    return {rect.left, rect.top};
}

SIZE UsageWindow::Size() const
{
    RECT rect{};
    if (hwnd_) GetWindowRect(hwnd_, &rect);
    return {rect.right - rect.left, rect.bottom - rect.top};
}

void UsageWindow::Show(bool visible)
{
    if (!hwnd_) return;

    if (visible)
    {
        Render();
        visible_ = true;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetTimer(hwnd_, kForegroundTimerId, kForegroundPollMs, nullptr);
        collapse_.Start(hwnd_, kCollapseMessage);
    }
    else
    {
        collapse_.Stop();
        KillTimer(hwnd_, kForegroundTimerId);
        visible_ = false;
        ShowWindow(hwnd_, SW_HIDE);
        dragging_ = false;
        dragged_ = false;
        closePressed_ = false;
    }
}

bool UsageWindow::Toggle()
{
    Show(!visible_);
    return visible_;
}

bool UsageWindow::IsVisible() const
{
    return visible_ && hwnd_ && IsWindowVisible(hwnd_);
}

void UsageWindow::SetData(std::vector<UsageRow> today, std::vector<UsageRow> week, bool storeOk)
{
    today_ = std::move(today);
    week_ = std::move(week);
    storeOk_ = storeOk;
    if (visible_) Render();
}

void UsageWindow::Render()
{
    if (!hwnd_) return;

    const int width = MulDiv(kWidthDip, dpi_, 96);
    const int height = MulDiv(kHeightDip, dpi_, 96);
    if (width <= 0 || height <= 0) return;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    if (!screen) return;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC memory = CreateCompatibleDC(screen);
    if (!dib || !memory || !bits)
    {
        if (dib) DeleteObject(dib);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return;
    }

    HGDIOBJ previous = SelectObject(memory, dib);

    {
        Bitmap canvas(width, height, width * 4, PixelFormat32bppARGB, static_cast<BYTE*>(bits));
        Graphics graphics(&canvas);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
        graphics.Clear(Color(0, 0, 0, 0));
        graphics.ScaleTransform(static_cast<REAL>(width) / kWidthDip,
                                static_cast<REAL>(height) / kHeightDip);

        FillRounded(graphics, RectF(0.5f, 0.5f, kWidthDip - 1.0f, kHeightDip - 1.0f), 12.0f,
                    kBackground);
        {
            GraphicsPath border;
            AddRoundRect(border, RectF(0.5f, 0.5f, kWidthDip - 1.0f, kHeightDip - 1.0f), 12.0f);
            Pen pen(kBorder, 1.0f);
            graphics.DrawPath(&pen, &border);
        }

        FontFamily uiFamily(L"Segoe UI");
        Font titleFont(&uiFamily, 15.0f, FontStyleBold, UnitPixel);
        Font tabFont(&uiFamily, 12.0f, FontStyleRegular, UnitPixel);
        Font tabActiveFont(&uiFamily, 12.0f, FontStyleBold, UnitPixel);
        Font rowFont(&uiFamily, 12.0f, FontStyleRegular, UnitPixel);
        Font footerFont(&uiFamily, 10.0f, FontStyleRegular, UnitPixel);

        const REAL innerWidth = kWidthDip - kPadding * 2;

        DrawText(graphics, L"流量统计", titleFont, kPrimary,
                 RectF(kPadding, kTitleTop, innerWidth - 24, 22));
        DrawText(graphics, L"\u2715", tabFont, kSecondary,
                 RectF(kWidthDip - kPadding - 16, kTitleTop, 16, 22), StringAlignmentFar);

        // Tabs: today and the last seven days.
        DrawText(graphics, L"今日", showWeek_ ? tabFont : tabActiveFont,
                 showWeek_ ? kSecondary : kPrimary, RectF(kPadding, kTabTop, kTabWidth, kTabHeight));
        DrawText(graphics, L"近 7 天", showWeek_ ? tabActiveFont : tabFont,
                 showWeek_ ? kPrimary : kSecondary,
                 RectF(kPadding + kTabWidth, kTabTop, kTabWidth + 8, kTabHeight));

        {
            const REAL underlineX =
                static_cast<REAL>(showWeek_ ? kPadding + kTabWidth : kPadding);
            const REAL underlineWidth = showWeek_ ? 56.0f : 30.0f;
            FillRounded(graphics,
                        RectF(underlineX, kTabTop + kTabHeight - 1, underlineWidth, 2), 1.0f, kDown);
        }

        const std::vector<UsageRow>& rows = showWeek_ ? week_ : today_;

        int64_t max = 1;
        for (size_t i = 0; i < rows.size() && i < kRows; ++i)
        {
            max = std::max(max, rows[i].down + rows[i].up);
        }

        const REAL nameWidth = 150;
        const REAL numberWidth = 58;
        const REAL downX = kPadding + nameWidth + 8;
        const REAL upX = downX + numberWidth + 6;

        for (size_t i = 0; i < rows.size() && i < kRows; ++i)
        {
            const UsageRow& row = rows[i];
            const REAL y = static_cast<REAL>(kRowsTop + static_cast<int>(i) * kRowHeight);

            // Proportional bar under the name column: drawing it behind the text
            // put a blue block over the file name, which read like a selection.
            const REAL barWidth = nameWidth * static_cast<REAL>(row.down + row.up) /
                                  static_cast<REAL>(max);
            FillRounded(graphics, RectF(kPadding, y + kRowHeight - 4, barWidth, 4), 2.0f, kBar);

            DrawText(graphics, row.name, rowFont, kPrimary,
                     RectF(kPadding, y, nameWidth, kRowHeight));
            DrawText(graphics, FormatBytes(row.down), rowFont, kDown,
                     RectF(downX, y, numberWidth, kRowHeight), StringAlignmentFar);
            DrawText(graphics, FormatBytes(row.up), rowFont, kUp,
                     RectF(upX, y, numberWidth, kRowHeight), StringAlignmentFar);
        }

        if (rows.empty())
        {
            DrawText(graphics, L"还没有累计数据", rowFont, kSecondary,
                     RectF(kPadding, kRowsTop, innerWidth, kRowHeight));
        }

        // Only time the process was running can be counted, and a failed write is
        // worth surfacing rather than silently showing zeros.
        const std::wstring footer =
            storeOk_ ? L"只统计程序运行期间" : L"未落盘：写入失败，仅内存有效";
        DrawText(graphics, footer, footerFont, kSecondary,
                 RectF(kPadding, kFooterTop, innerWidth, 14));
    }

    POINT source{0, 0};
    SIZE windowSize{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen, nullptr, &windowSize, memory, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, previous);
    DeleteDC(memory);
    DeleteObject(dib);
    ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK UsageWindow::StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* self = static_cast<UsageWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<UsageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->Handle(message, wParam, lParam);

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool UsageWindow::InCloseButton(int x, int y) const
{
    const int left = MulDiv(kWidthDip - kPadding - 16, dpi_, 96);
    const int top = MulDiv(kTitleTop, dpi_, 96);
    const int size = MulDiv(22, dpi_, 96);
    return x >= left && x <= left + size && y >= top && y <= top + size;
}

bool UsageWindow::InTodayTab(int x, int y) const
{
    const int left = MulDiv(kPadding, dpi_, 96);
    const int top = MulDiv(kTabTop, dpi_, 96);
    const int width = MulDiv(kTabWidth, dpi_, 96);
    const int height = MulDiv(kTabHeight, dpi_, 96);
    return x >= left && x <= left + width && y >= top && y <= top + height;
}

bool UsageWindow::InWeekTab(int x, int y) const
{
    const int left = MulDiv(kPadding + kTabWidth, dpi_, 96);
    const int top = MulDiv(kTabTop, dpi_, 96);
    const int width = MulDiv(kTabWidth + 8, dpi_, 96);
    const int height = MulDiv(kTabHeight, dpi_, 96);
    return x >= left && x <= left + width && y >= top && y <= top + height;
}

LRESULT UsageWindow::Handle(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER:
        if (wParam == kForegroundTimerId && visible_ && !dragging_ && collapse_.ForegroundLeft())
        {
            Show(false);
        }
        return 0;

    case kCollapseMessage:
        if (visible_ && !dragging_) Show(false);
        return 0;

    case WM_LBUTTONDOWN:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        // Tabs are handled before the drag: a tab click must never move the window.
        if (InTodayTab(x, y))
        {
            showWeek_ = false;
            Render();
            return 0;
        }
        if (InWeekTab(x, y))
        {
            showWeek_ = true;
            Render();
            return 0;
        }

        closePressed_ = InCloseButton(x, y);
        if (!closePressed_)
        {
            POINT point{x, y};
            ClientToScreen(hwnd_, &point);
            RECT rect{};
            GetWindowRect(hwnd_, &rect);
            dragStart_ = point;
            dragOrigin_ = POINT{rect.left, rect.top};
            dragging_ = true;
            dragged_ = false;
            SetCapture(hwnd_);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (dragging_)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd_, &point);
            const int dx = point.x - dragStart_.x;
            const int dy = point.y - dragStart_.y;
            const int threshold = MulDiv(kDragThresholdDip, dpi_, 96);
            if (!dragged_ && (std::abs(dx) >= threshold || std::abs(dy) >= threshold))
            {
                dragged_ = true;
            }
            if (dragged_)
            {
                SetWindowPos(hwnd_, HWND_TOPMOST, dragOrigin_.x + dx, dragOrigin_.y + dy, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
    {
        const bool wasDragging = dragging_;
        const bool moved = dragged_;
        const bool onClose = closePressed_;
        if (wasDragging) ReleaseCapture();
        dragging_ = false;
        dragged_ = false;
        closePressed_ = false;

        if (onClose && !moved) Show(false);
        return 0;
    }

    case WM_CAPTURECHANGED:
        dragging_ = false;
        dragged_ = false;
        return 0;

    case WM_DPICHANGED:
        dpi_ = HIWORD(wParam);
        if (const auto* rect = reinterpret_cast<const RECT*>(lParam))
        {
            SetWindowPos(hwnd_, HWND_TOPMOST, rect->left, rect->top, rect->right - rect->left,
                         rect->bottom - rect->top, SWP_NOACTIVATE);
        }
        Render();
        return 0;

    case WM_DESTROY:
        collapse_.Stop();
        hwnd_ = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}
