#pragma once

#include "common.h"

// Layered 76x76 DIP ball: up rate on top, KB/s in the middle, down rate below.
// Same visual constants as the WPF BallRoot (background #E614161A, 1 px border
// #2AFFFFFF, up #FF9E6D, down #4CC2FF, secondary #8A8F98).
class BallWindow
{
public:
    bool Create(HINSTANCE instance, int x, int y, HWND notify);
    HWND Hwnd() const { return hwnd_; }

    void SetRates(double downBytesPerSecond, double upBytesPerSecond);
    void MoveTo(int x, int y);
    void Show(bool visible);
    bool Toggle();
    // User intent: true even while a full-screen app temporarily hides the ball.
    bool IsVisible() const;

    // SPEC 17: a full-screen app on the ball's monitor hides the ball too, the
    // same rule the taskbar readout follows.
    void UpdateFullscreen(bool fullscreen);

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Render();
    void ApplyVisibility();
    int PixelSize() const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HWND notify_ = nullptr;
    double down_ = 0;
    double up_ = 0;
    int dpi_ = 96;
    bool visible_ = true;
    bool suppressed_ = false;
};
