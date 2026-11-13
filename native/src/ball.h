#pragma once

#include "common.h"

// Layered 76x76 DIP ball: up rate on top, MB/s in the middle, down rate below.
// Same visual constants as the WPF BallRoot (background #E614161A, 1 px border
// #2AFFFFFF, up #FF9E6D, down #4CC2FF, secondary #8A8F98).
class BallWindow
{
public:
    bool Create(HINSTANCE instance, int x, int y);
    HWND Hwnd() const { return hwnd_; }

    void SetRates(double downBytesPerSecond, double upBytesPerSecond);
    void Show(bool visible);
    bool Toggle();
    bool IsVisible() const;

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Render();
    int PixelSize() const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    double down_ = 0;
    double up_ = 0;
    int dpi_ = 96;
    bool visible_ = true;
};
