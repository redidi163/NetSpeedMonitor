#include "panel.h"

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
constexpr int kPanelWidthDip = 280;
constexpr int kPanelHeightDip = 348;
constexpr int kTop = 10;
constexpr int kPadding = 14;
constexpr int kRowHeight = 24;
constexpr int kRowsTop = 162;
constexpr int kNameWidth = 164;
constexpr int kDownWidth = 40;
constexpr int kUpWidth = 34;
constexpr int kDragThresholdDip = 6;
constexpr UINT_PTR kForegroundTimerId = 1;
constexpr UINT kForegroundPollMs = 200;
// The panel opens mid-focus-handover (tray menu closing, or --panel while the
// desktop settles); ignore the poll until the foreground has had time to land.
constexpr ULONGLONG kOpenGraceMs = 500;
// Posted by the mouse hook; handled on the UI thread so Show(false) never runs
// inside the hook callback.
constexpr UINT kCollapseMessage = WM_APP + 4;

const Color kBackground(0xE6, 0x14, 0x16, 0x1A);
const Color kBorder(0x2A, 0xFF, 0xFF, 0xFF);
const Color kPrimary(0xFF, 0xE8, 0xEA, 0xED);
const Color kSecondary(0xFF, 0x8A, 0x8F, 0x98);
const Color kDown(0xFF, 0x4C, 0xC2, 0xFF);
const Color kUp(0xFF, 0xFF, 0x9E, 0x6D);
const Color kBar(0x1F, 0x4C, 0xC2, 0xFF);
const Color kTrack(0x14, 0xFF, 0xFF, 0xFF);

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

void DrawArrow(Graphics& graphics, Font& font, const Color& color, REAL x, REAL y,
               const wchar_t* glyph, REAL width)
{
    DrawText(graphics, glyph, font, color, RectF(x, y, width, 26), StringAlignmentCenter);
}

// GetDpiForWindow reports 96 for a window that has never been shown, which
// silently renders the whole panel at 1x on a 144 dpi desktop. The monitor
// under the creation point is the reliable source.
UINT DpiForPoint(int x, int y)
{
    POINT point{x, y};
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) return dpiX;
    return 96;
}


REAL Measure(Graphics& graphics, const std::wstring& text, Font& font)
{
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    RectF box;
    graphics.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &format, &box);
    return box.Width;
}
} // namespace

bool PanelWindow::Create(HINSTANCE instance, int x, int y)
{
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NSM.Panel";
    RegisterClassExW(&wc);

    dpi_ = static_cast<int>(DpiForPoint(x, y));

    const int width = MulDiv(kPanelWidthDip, dpi_, 96);
    const int height = MulDiv(kPanelHeightDip, dpi_, 96);

    // WS_EX_NOACTIVATE keeps the panel from stealing focus from whatever the
    // user is working in. The price is that the panel never becomes foreground,
    // so an outside click cannot be seen through WM_ACTIVATE; SPEC 12 detects it
    // with the low-level mouse hook installed while the panel is visible.
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                            L"NSM.Panel", L"NetSpeedMonitor", WS_POPUP, x, y, width, height,
                            nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    // A window that has never been shown reports 96 dpi even on a 144 dpi
    // desktop; show it for one frame (it has no layered surface yet, so nothing
    // is painted) to get the real value, then hide it again.
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    dpi_ = static_cast<int>(GetDpiForWindow(hwnd_));
    if (dpi_ <= 0) dpi_ = 96;
    ShowWindow(hwnd_, SW_HIDE);

    const int actualWidth = MulDiv(kPanelWidthDip, dpi_, 96);
    const int actualHeight = MulDiv(kPanelHeightDip, dpi_, 96);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, actualWidth, actualHeight, SWP_NOACTIVATE);

    Render();
    return true;
}

void PanelWindow::SetDpi(int dpi)
{
    if (dpi <= 0) return;
    dpi_ = dpi;
    if (!hwnd_) return;

    // Enforce the size every time: the window can still be at its 96 dpi size
    // even though dpi_ already changed, so comparing dpi alone is not enough.
    const int width = MulDiv(kPanelWidthDip, dpi_, 96);
    const int height = MulDiv(kPanelHeightDip, dpi_, 96);

    RECT current{};
    GetWindowRect(hwnd_, &current);
    if (current.right - current.left != width || current.bottom - current.top != height)
    {
        SetWindowPos(hwnd_, HWND_TOPMOST, current.left, current.top, width, height,
                     SWP_NOACTIVATE);
    }

    if (visible_) Render();
}

void PanelWindow::MoveTo(int x, int y)
{
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void PanelWindow::Show(bool visible)
{
    if (!hwnd_) return;

    if (visible)
    {
        Render();
        visible_ = true;
        // A NOACTIVATE window does not take focus, so the foreground window at
        // this moment is the user's app, not the panel. It is the reference the
        // timer compares against to catch alt-tab and other focus changes.
        foregroundAtOpen_ = GetForegroundWindow();
        openedAt_ = GetTickCount64();
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetTimer(hwnd_, kForegroundTimerId, kForegroundPollMs, nullptr);
        InstallMouseHook();
    }
    else
    {
        // Tear the hook down first: it posts kCollapseMessage, and a stale post
        // after the panel closed would hit the next open.
        RemoveMouseHook();
        KillTimer(hwnd_, kForegroundTimerId);
        visible_ = false;
        ShowWindow(hwnd_, SW_HIDE);
        dragging_ = false;
        dragged_ = false;
        closePressed_ = false;
    }
}

PanelWindow* PanelWindow::hookOwner_ = nullptr;

bool PanelWindow::IsOurWindowAt(POINT point) const
{
    // The panel itself is checked by rectangle: it is a layered window with
    // rounded corners, where WindowFromPoint skips the fully transparent pixels.
    RECT rect{};
    if (GetWindowRect(hwnd_, &rect) && PtInRect(&rect, point)) return true;

    // Anything else of ours: the ball (a click there toggles the panel, so it
    // must not be treated as an outside click) and the host window.
    const HWND under = WindowFromPoint(point);
    if (!under) return false;
    const HWND root = GetAncestor(under, GA_ROOT);
    DWORD pid = 0;
    GetWindowThreadProcessId(root ? root : under, &pid);
    return pid == GetCurrentProcessId();
}

void PanelWindow::InstallMouseHook()
{
    if (mouseHook_) return;
    hookOwner_ = this;
    mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandleW(nullptr), 0);
}

void PanelWindow::RemoveMouseHook()
{
    if (!mouseHook_) return;
    UnhookWindowsHookEx(mouseHook_);
    mouseHook_ = nullptr;
    hookOwner_ = nullptr;
}

LRESULT CALLBACK PanelWindow::MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    // WH_MOUSE_LL runs on this thread's message loop, but still keep the work
    // here to a hit test: the hook sits in the system's input path.
    if (code == HC_ACTION && hookOwner_ && hookOwner_->visible_ &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN))
    {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (!hookOwner_->IsOurWindowAt(info->pt))
        {
            PostMessageW(hookOwner_->hwnd_, kCollapseMessage, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void PanelWindow::SetNotify(HWND host, UINT message)
{
    host_ = host;
    notifyMessage_ = message;
}

bool PanelWindow::Toggle()
{
    Show(!visible_);
    return visible_;
}

bool PanelWindow::IsVisible() const
{
    return visible_ && hwnd_ && IsWindowVisible(hwnd_);
}

POINT PanelWindow::Position() const
{
    RECT rect{};
    if (hwnd_) GetWindowRect(hwnd_, &rect);
    return {rect.left, rect.top};
}

void PanelWindow::SetData(const std::wstring& adapterName, double down, double up,
                          const std::vector<float>& trend, const std::vector<ProcessRow>& rows,
                          const std::wstring& footer)
{
    adapterName_ = adapterName;
    down_ = down;
    up_ = up;
    trend_ = trend;
    rows_ = rows;
    footer_ = footer;

    if (visible_) Render();
}

void PanelWindow::Render()
{
    if (!hwnd_) return;

    const int width = MulDiv(kPanelWidthDip, dpi_, 96);
    const int height = MulDiv(kPanelHeightDip, dpi_, 96);
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

        const REAL scale = static_cast<REAL>(width) / kPanelWidthDip;
        graphics.ScaleTransform(scale, scale);

        FillRounded(graphics, RectF(0.5f, 0.5f, kPanelWidthDip - 1.0f, kPanelHeightDip - 1.0f),
                    12.0f, kBackground);
        {
            GraphicsPath border;
            AddRoundRect(border, RectF(0.5f, 0.5f, kPanelWidthDip - 1.0f, kPanelHeightDip - 1.0f),
                         12.0f);
            Pen pen(kBorder, 1.0f);
            graphics.DrawPath(&pen, &border);
        }

        FontFamily uiFamily(L"Segoe UI");
        FontFamily symbolFamily(L"Segoe UI Symbol");

        Font headerFont(&uiFamily, 11.0f, FontStyleRegular, UnitPixel);
        Font bigFont(&uiFamily, 26.0f, FontStyleBold, UnitPixel);
        Font upFont(&uiFamily, 18.0f, FontStyleBold, UnitPixel);
        Font unitFont(&uiFamily, 11.0f, FontStyleRegular, UnitPixel);
        Font sectionFont(&uiFamily, 10.0f, FontStyleRegular, UnitPixel);
        Font rowFont(&uiFamily, 11.0f, FontStyleRegular, UnitPixel);
        Font footerFont(&uiFamily, 10.0f, FontStyleRegular, UnitPixel);
        Font bigArrow(&symbolFamily, 18.0f, FontStyleRegular, UnitPixel);
        Font smallArrow(&symbolFamily, 16.0f, FontStyleRegular, UnitPixel);

        const REAL innerWidth = kPanelWidthDip - kPadding * 2;

        DrawText(graphics, adapterName_, headerFont, kSecondary,
                 RectF(kPadding, kTop, innerWidth - 20, 18));
        DrawText(graphics, L"\u2715", headerFont, kSecondary,
                 RectF(kPanelWidthDip - kPadding - 14, kTop, 14, 18), StringAlignmentFar);

        // Down total: the big number.
        DrawArrow(graphics, bigArrow, kDown, kPadding, 32, L"\u2193", 18);
        const std::wstring downText = FormatKBps(down_);
        DrawText(graphics, downText, bigFont, kPrimary, RectF(kPadding + 20, 32, 140, 34),
                 StringAlignmentNear, StringAlignmentCenter);
        const REAL downWidth = Measure(graphics, downText, bigFont);
        DrawText(graphics, L"KB/s", unitFont, kSecondary,
                 RectF(kPadding + 22 + downWidth, 44, 40, 18));

        // Up total: one step smaller, same left edge.
        DrawArrow(graphics, smallArrow, kUp, kPadding, 70, L"\u2191", 16);
        const std::wstring upText = FormatKBps(up_);
        DrawText(graphics, upText, upFont, kPrimary, RectF(kPadding + 18, 70, 120, 26),
                 StringAlignmentNear, StringAlignmentCenter);
        const REAL upWidth = Measure(graphics, upText, upFont);
        DrawText(graphics, L"KB/s", unitFont, kSecondary, RectF(kPadding + 20 + upWidth, 76, 40, 18));

        // 60 s sparkline, newest on the right.
        {
            const RectF plot(kPadding, 104, innerWidth, 34);
            FillRounded(graphics, RectF(plot.X, plot.Y + plot.Height - 1, plot.Width, 1), 0.5f, kTrack);

            if (trend_.size() >= 2)
            {
                float max = 0;
                for (float value : trend_) max = std::max(max, value);
                if (max <= 0) max = 1;

                std::vector<PointF> points;
                points.reserve(trend_.size());
                const REAL step = plot.Width / 59.0f;
                for (size_t i = 0; i < trend_.size(); ++i)
                {
                    const REAL x = plot.X + step * static_cast<REAL>(59 - (trend_.size() - 1 - i));
                    const REAL y = plot.Y + plot.Height - (trend_[i] / max) * plot.Height;
                    points.push_back(PointF(x, y));
                }

                Pen pen(kDown, 1.5f);
                graphics.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
            }
        }

        DrawText(graphics, L"进程速率", sectionFont, kSecondary,
                 RectF(kPadding, 146, innerWidth, 14));

        for (size_t i = 0; i < rows_.size() && i < 5; ++i)
        {
            const ProcessRow& row = rows_[i];
            const REAL y = static_cast<REAL>(kRowsTop + static_cast<int>(i) * kRowHeight);

            FillRounded(graphics, RectF(kPadding, y + 4, static_cast<REAL>(row.barWidth), 16), 3.0f,
                        kBar);

            DrawText(graphics, row.name, rowFont, kPrimary,
                     RectF(kPadding, y, static_cast<REAL>(kNameWidth), kRowHeight));
            DrawText(graphics, row.downText, rowFont, kDown,
                     RectF(kPadding + kNameWidth + 8, y, static_cast<REAL>(kDownWidth), kRowHeight),
                     StringAlignmentFar);
            DrawText(graphics, row.upText, rowFont, kUp,
                     RectF(kPadding + kNameWidth + 8 + kDownWidth + 6, y, static_cast<REAL>(kUpWidth),
                           kRowHeight),
                     StringAlignmentFar);
        }

        DrawText(graphics, footer_, footerFont, kSecondary,
                 RectF(kPadding, 326, innerWidth, 14));
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

LRESULT CALLBACK PanelWindow::StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* self = static_cast<PanelWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<PanelWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->Handle(message, wParam, lParam);

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool PanelWindow::InCloseButton(int x, int y) const
{
    const int closeLeft = MulDiv(kPanelWidthDip - kPadding - 14, dpi_, 96);
    const int closeTop = MulDiv(kTop, dpi_, 96);
    const int closeSize = MulDiv(18, dpi_, 96);
    return x >= closeLeft && x <= closeLeft + closeSize && y >= closeTop &&
           y <= closeTop + closeSize;
}

LRESULT PanelWindow::Handle(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER:
        // Alt-tab and other focus changes that never produce a mouse click. The
        // grace period swallows the focus handover that follows opening: the
        // tray menu closing, or --panel starting while the desktop settles.
        if (wParam == kForegroundTimerId && visible_ && !dragging_ &&
            GetTickCount64() - openedAt_ >= kOpenGraceMs)
        {
            const HWND foreground = GetForegroundWindow();
            if (foreground != foregroundAtOpen_ && foreground != hwnd_) Show(false);
        }
        return 0;

    case kCollapseMessage:
        // Posted by the mouse hook: a click landed on a window that is not ours.
        if (visible_ && !dragging_) Show(false);
        return 0;

    case WM_LBUTTONDOWN:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
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

        if (onClose && !moved)
        {
            Show(false);
            return 0;
        }

        if (wasDragging && moved && host_ && notifyMessage_)
        {
            const POINT position = Position();
            PostMessageW(host_, notifyMessage_, static_cast<WPARAM>(position.x),
                         static_cast<LPARAM>(position.y));
        }
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
        RemoveMouseHook();
        hwnd_ = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}
