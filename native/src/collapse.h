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
// One process-wide hook serves all active popups: it is installed while any popup
// is visible and removed when the last one hides, so a stale post can never
// collapse the next open. The panel and usage list can be up at the same time,
// so every Start() registers its own owner and the single hook fans a click out
// to each registered one.
class OutsideCollapse
{
public:
    void Start(HWND target, UINT message);
    void Stop();
    bool Active() const { return registered_; }

    // True once the open grace period has passed and the foreground moved away.
    bool ForegroundLeft() const;

private:
    static LRESULT CALLBACK Proc(int code, WPARAM wParam, LPARAM lParam);
    bool Hit(POINT point) const;

    // Two popups exist (panel + usage list); leave headroom.
    static constexpr int kMaxOwners = 4;
    static OutsideCollapse* owners_[kMaxOwners];
    static int ownerCount_;
    static HHOOK hook_; // one shared process-wide low-level hook

    bool registered_ = false;
    HWND target_ = nullptr;
    UINT message_ = 0;
    HWND foregroundAtOpen_ = nullptr;
    ULONGLONG openedAt_ = 0;
};
