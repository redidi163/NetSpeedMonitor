#pragma once

#include "battery.h"
#include "common.h"

// One-line readout docked into the taskbar, left of the clock:
//   ↑41.0 ↓1496.0 KB/s · 内存 42% · CPU 12%   [86%]
// Speed comes from the same AdapterSampler as the ball (SPEC 8: KB/s only);
// memory and CPU are system-wide percentages sampled on the same 1 s tick.
// The trailing pill is the 2.4G mouse battery (native-16): its fill colour
// follows the level and it blinks below 20% while discharging.
class TaskbarWindow
{
public:
    bool Create(HINSTANCE instance);
    HWND Hwnd() const { return hwnd_; }

    // Called from the 1 s tick. Re-docks if Explorer restarted, re-measures the
    // text and repaints only when something visible changed.
    void Update(double downBytesPerSecond, double upBytesPerSecond);

    // Called from the same tick. The pill appears only once a receiver is seen,
    // so a desktop without this mouse keeps the old single-line layout.
    void SetBattery(const BatterySnapshot& state);

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool Dock();
    bool EnsureFont(int dpi);
    void ReleaseFont();
    void Measure(const std::wstring& text);
    void Repaint();
    double SampleCpu();

    bool BatteryVisible() const { return battery_.present; }
    bool BatteryLow() const;
    RECT PillRect(const RECT& client) const;
    void UpdateBlinkTimer();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND taskbar_ = nullptr;
    HFONT font_ = nullptr;
    HFONT pillFont_ = nullptr;
    std::wstring text_;
    int dpi_ = 96;
    int padding_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool light_ = true;

    BatterySnapshot battery_{};
    bool low_ = false;      // hysteresis latch: enter at 20%, leave at 23%
    bool blinkOn_ = true;   // toggled by the 500 ms timer while low_
    bool blinkTimer_ = false;
    int pillWidth_ = 0;
    int pillHeight_ = 0;

    uint64_t lastIdle_ = 0;
    uint64_t lastKernel_ = 0;
    uint64_t lastUser_ = 0;
    bool cpuPrimed_ = false;
    double cpuPercent_ = 0;
};
