#pragma once

#include "common.h"

// Outside-click detection for the WS_EX_NOACTIVATE popups (the panel and the
// usage list).
//
// Such a window never becomes foreground, so WM_ACTIVATE cannot report the click
// that leaves the foreground window unchanged - clicking back into the app that
// was already foreground when the popup opened (native-10). Two mechanisms run
// while the window is visible:
//   - a WH_MOUSE_LL hook posts `message` to the window when a button goes down
//     somewhere outside it,
//   - ForegroundLeft() covers focus changes that never produce a click (alt-tab).
// The hook is installed only while visible and always torn down before hiding:
// a stale post would otherwise collapse the next open immediately.
class OutsideCollapse
{
public:
    void Start(HWND target, UINT message);
    void Stop();
    bool Active() const { return hook_ != nullptr; }

    // True once the open grace period has passed and the foreground moved away.
    bool ForegroundLeft() const;

private:
    static LRESULT CALLBACK Proc(int code, WPARAM wParam, LPARAM lParam);
    bool Hit(POINT point) const;

    static OutsideCollapse* owner_;

    HWND target_ = nullptr;
    UINT message_ = 0;
    HHOOK hook_ = nullptr;
    HWND foregroundAtOpen_ = nullptr;
    ULONGLONG openedAt_ = 0;
};
