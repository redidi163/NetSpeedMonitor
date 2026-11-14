#include "ball.h"

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;

namespace
{
constexpr int kBallDip = 76;

const Color kBackground(0xE6, 0x14, 0x16, 0x1A);
const Color kBorder(0x2A, 0xFF, 0xFF, 0xFF);
const Color kUp(0xFF, 0xFF, 0x9E, 0x6D);
const Color kDown(0xFF, 0x4C, 0xC2, 0xFF);
const Color kSecondary(0xFF, 0x8A, 0x8F, 0x98);

void DrawRate(Graphics& graphics, const Font& arrowFont, const Font& numberFont,
              const wchar_t* arrow, const std::wstring& text, const Color& color,
              REAL centerY, int width)
{
    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentCenter);

    RectF arrowBox;
    RectF textBox;
    graphics.MeasureString(arrow, -1, &arrowFont, PointF(0, 0), &format, &arrowBox);
    graphics.MeasureString(text.c_str(), -1, &numberFont, PointF(0, 0), &format, &textBox);

    const REAL gap = 2.0f;
    const REAL total = arrowBox.Width + gap + textBox.Width;
    const REAL x = (width - total) / 2.0f;

    SolidBrush brush(color);
    graphics.DrawString(arrow, -1, &arrowFont, PointF(x, centerY), &format, &brush);
    graphics.DrawString(text.c_str(), -1, &numberFont,
                        PointF(x + arrowBox.Width + gap, centerY), &format, &brush);
}
} // namespace

bool BallWindow::Create(HINSTANCE instance, int x, int y, HWND notify)
{
    instance_ = instance;
    notify_ = notify;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = L"NSM.Ball";
    RegisterClassExW(&wc);

    const int size = PixelSize();
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"NSM.Ball", L"NetSpeedMonitor", WS_POPUP,
        x, y, size, size, nullptr, nullptr, instance, this);

    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(hwnd_);
    const int actual = PixelSize();
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, actual, actual, SWP_NOACTIVATE);

    Render();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    visible_ = true;
    return true;
}

void BallWindow::SetRates(double downBytesPerSecond, double upBytesPerSecond)
{
    down_ = downBytesPerSecond;
    up_ = upBytesPerSecond;
    Render();
}

void BallWindow::Show(bool visible)
{
    if (!hwnd_) return;
    visible_ = visible;
    ApplyVisibility();
}

void BallWindow::ApplyVisibility()
{
    if (!hwnd_) return;
    ShowWindow(hwnd_, (visible_ && !suppressed_) ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void BallWindow::UpdateFullscreen(bool fullscreen)
{
    if (!hwnd_ || fullscreen == suppressed_) return;
    suppressed_ = fullscreen;
    ApplyVisibility();
}

void BallWindow::MoveTo(int x, int y)
{
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

bool BallWindow::Toggle()
{
    Show(!visible_);
    return visible_;
}

bool BallWindow::IsVisible() const
{
    // User intent, not the window's current state: a full-screen app hides the
    // ball without changing what the tray menu should say.
    return visible_ && hwnd_ != nullptr;
}

int BallWindow::PixelSize() const
{
    return MulDiv(kBallDip, dpi_, 96);
}

void BallWindow::Render()
{
    if (!hwnd_) return;

    const int size = PixelSize();
    if (size <= 0) return;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
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
        Bitmap canvas(size, size, size * 4, PixelFormat32bppARGB, static_cast<BYTE*>(bits));
        Graphics graphics(&canvas);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
        graphics.Clear(Color(0, 0, 0, 0));

        SolidBrush background(kBackground);
        graphics.FillEllipse(&background, 0.0f, 0.0f, static_cast<REAL>(size), static_cast<REAL>(size));

        Pen border(kBorder, 1.0f);
        graphics.DrawEllipse(&border, 0.5f, 0.5f, size - 1.0f, size - 1.0f);

        const REAL scale = static_cast<REAL>(size) / kBallDip;
        FontFamily uiFamily(L"Segoe UI");
        FontFamily symbolFamily(L"Segoe UI Symbol");

        Font upFont(&uiFamily, 12.0f * scale, FontStyleRegular, UnitPixel);
        Font downFont(&uiFamily, 13.0f * scale, FontStyleBold, UnitPixel);
        Font unitFont(&uiFamily, 8.0f * scale, FontStyleRegular, UnitPixel);
        Font upArrowFont(&symbolFamily, 12.0f * scale, FontStyleRegular, UnitPixel);
        Font downArrowFont(&symbolFamily, 13.0f * scale, FontStyleRegular, UnitPixel);

        DrawRate(graphics, upArrowFont, upFont, L"\u2191", FormatKBps(up_), kUp,
                 size * 0.27f, size);
        DrawRate(graphics, downArrowFont, downFont, L"\u2193", FormatKBps(down_), kDown,
                 size * 0.73f, size);

        StringFormat unitFormat;
        unitFormat.SetAlignment(StringAlignmentCenter);
        unitFormat.SetLineAlignment(StringAlignmentCenter);
        SolidBrush unitBrush(kSecondary);
        graphics.DrawString(L"KB/s", -1, &unitFont,
                            PointF(size / 2.0f, size * 0.5f), &unitFormat, &unitBrush);
    }

    POINT source{0, 0};
    SIZE windowSize{size, size};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen, nullptr, &windowSize, memory, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, previous);
    DeleteDC(memory);
    DeleteObject(dib);
    ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK BallWindow::StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* self = static_cast<BallWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<BallWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->Handle(message, wParam, lParam);

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT BallWindow::Handle(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DPICHANGED:
        dpi_ = HIWORD(wParam);
        if (const auto* rect = reinterpret_cast<const RECT*>(lParam))
        {
            SetWindowPos(hwnd_, HWND_TOPMOST, rect->left, rect->top,
                         rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOACTIVATE);
        }
        Render();
        return 0;

    case WM_LBUTTONUP:
        if (notify_) PostMessageW(notify_, WM_APP + 2, 0, 0);
        return 0;

    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
}
