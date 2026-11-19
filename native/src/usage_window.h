#pragma once

#include "collapse.h"
#include "common.h"
#include "usage.h"

#include <vector>

// 320x420 DIP ranking window (native-13): today's or the last seven days' top 10
// exes by payload bytes. Same no-activate layered treatment as the panel, so it
// never takes focus and disappears on an outside click. Rows arrive already
// sorted; only the first 10 are drawn.
class UsageWindow
{
public:
    bool Create(HINSTANCE instance);

    HWND Hwnd() const { return hwnd_; }

    void Show(bool visible);
    bool Toggle();
    bool IsVisible() const;

    // Adopts the DPI of the window it opens next to. A hidden layered window
    // reports 96 until it has been shown, so probing it at create time is not
    // reliable; the caller knows the DPI of the visible window (ball or panel).
    void SetDpi(int dpi);

    void MoveTo(int x, int y);
    POINT Position() const;
    SIZE Size() const;

    void SetData(std::vector<UsageRow> today, std::vector<UsageRow> week, bool storeOk);

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Render();
    void ApplyDpi();

    bool InCloseButton(int x, int y) const;
    bool InTodayTab(int x, int y) const;
    bool InWeekTab(int x, int y) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    int dpi_ = 96;
    bool visible_ = false;

    OutsideCollapse collapse_;

    bool dragging_ = false;
    bool dragged_ = false;
    POINT dragStart_{};
    POINT dragOrigin_{};
    bool closePressed_ = false;

    bool showWeek_ = false;
    bool storeOk_ = true;
    std::vector<UsageRow> today_;
    std::vector<UsageRow> week_;
};
