#include "tray.h"

#include <gdiplus.h>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

namespace
{
// 32x32 tray icon drawn at runtime: dark disc, accent ring, down arrow.
HICON CreateAppIcon(int size)
{
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;

    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header),
                                     DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits)
    {
        if (color) DeleteObject(color);
        ReleaseDC(nullptr, screen);
        return nullptr;
    }

    {
        Bitmap canvas(size, size, size * 4, PixelFormat32bppARGB, static_cast<BYTE*>(bits));
        Graphics graphics(&canvas);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.Clear(Color(0, 0, 0, 0));

        SolidBrush background(Color(0xFF, 0x14, 0x16, 0x1A));
        graphics.FillEllipse(&background, 0.0f, 0.0f, static_cast<REAL>(size), static_cast<REAL>(size));

        Pen ring(Color(0xFF, 0x4C, 0xC2, 0xFF), size / 14.0f);
        graphics.DrawEllipse(&ring, size / 20.0f, size / 20.0f,
                             size - size / 10.0f, size - size / 10.0f);

        Pen arrow(Color(0xFF, 0x4C, 0xC2, 0xFF), size / 10.0f);
        arrow.SetStartCap(LineCapRound);
        arrow.SetEndCap(LineCapRound);
        const REAL cx = size / 2.0f;
        graphics.DrawLine(&arrow, cx, size * 0.28f, cx, size * 0.70f);
        graphics.DrawLine(&arrow, cx, size * 0.70f, cx - size * 0.16f, size * 0.54f);
        graphics.DrawLine(&arrow, cx, size * 0.70f, cx + size * 0.16f, size * 0.54f);
    }

    const int maskStride = ((size + 15) / 16) * 2;
    std::vector<BYTE> maskBits(static_cast<size_t>(maskStride) * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);

    DeleteObject(color);
    if (mask) DeleteObject(mask);
    ReleaseDC(nullptr, screen);
    return icon;
}
} // namespace

TrayIcon::~TrayIcon()
{
    Remove();
}

bool TrayIcon::Create(HINSTANCE /*instance*/, HWND owner, UINT callbackMessage)
{
    icon_ = CreateAppIcon(32);

    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data_.uCallbackMessage = callbackMessage;
    data_.hIcon = icon_;
    wcscpy_s(data_.szTip, L"NetSpeedMonitor");

    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    return added_;
}

void TrayIcon::SetTooltip(const std::wstring& text)
{
    if (!added_) return;
    wcsncpy_s(data_.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::Remove()
{
    if (added_)
    {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }

    if (icon_)
    {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}
