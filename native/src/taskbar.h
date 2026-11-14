#pragma once

#include "common.h"

// One-line readout docked into the taskbar, left of the clock:
//   ↑41.0 ↓1496.0 KB/s · 内存 42% · CPU 12%
// Speed comes from the same AdapterSampler as the ball (SPEC 8: KB/s only);
// memory and CPU are system-wide percentages sampled on the same 1 s tick.
class TaskbarWindow
{
public:
    bool Create(HINSTANCE instance);
    HWND Hwnd() const { return hwnd_; }

    // Called from the 1 s tick. Re-docks if Explorer restarted, re-measures the
    // text and repaints only when something visible changed.
    void Update(double downBytesPerSecond, double upBytesPerSecond);

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool Dock();
    bool EnsureFont(int dpi);
    void ReleaseFont();
    void Measure(const std::wstring& text);
    void Repaint();
    double SampleCpu();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND taskbar_ = nullptr;
    HFONT font_ = nullptr;
    std::wstring text_;
    int dpi_ = 96;
    int padding_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool light_ = true;

    uint64_t lastIdle_ = 0;
    uint64_t lastKernel_ = 0;
    uint64_t lastUser_ = 0;
    bool cpuPrimed_ = false;
    double cpuPercent_ = 0;
};
