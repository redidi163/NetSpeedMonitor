#include "collapse.h"

namespace
{
// The popup opens mid-focus-handover (tray menu closing, --panel while the
// desktop settles); ignore the foreground poll until it has had time to land.
constexpr ULONGLONG kOpenGraceMs = 500;
} // namespace

OutsideCollapse* OutsideCollapse::owners_[OutsideCollapse::kMaxOwners] = {};
int OutsideCollapse::ownerCount_ = 0;
HHOOK OutsideCollapse::hook_ = nullptr;

void OutsideCollapse::Start(HWND target, UINT message)
{
    if (!target) return;

    Stop(); // a second Start must not register this instance twice

    target_ = target;
    message_ = message;
    foregroundAtOpen_ = GetForegroundWindow();
    openedAt_ = GetTickCount64();
    registered_ = true;

    if (ownerCount_ < kMaxOwners) owners_[ownerCount_++] = this;

    // One hook for every owner: installing one per popup would make the second
    // hook's callbacks all read a single static owner (native-10 regression).
    if (!hook_) hook_ = SetWindowsHookExW(WH_MOUSE_LL, Proc, GetModuleHandleW(nullptr), 0);
}

void OutsideCollapse::Stop()
{
    // Remove this instance from the owner table.
    for (int i = 0; i < ownerCount_; ++i)
    {
        if (owners_[i] == this)
        {
            owners_[i] = owners_[ownerCount_ - 1]; // swap-remove, order is irrelevant
            owners_[--ownerCount_] = nullptr;
            break;
        }
    }
    registered_ = false;

    // Tear the shared hook down only after the last owner is gone: a stale post
    // after a popup closed would collapse the next open.
    if (ownerCount_ == 0 && hook_)
    {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }

    target_ = nullptr;
    message_ = 0;
    foregroundAtOpen_ = nullptr;
    openedAt_ = 0;
}

bool OutsideCollapse::ForegroundLeft() const
{
    if (!target_ || !IsWindowVisible(target_)) return false;
    if (GetTickCount64() - openedAt_ < kOpenGraceMs) return false;

    const HWND foreground = GetForegroundWindow();
    return foreground != foregroundAtOpen_ && foreground != target_;
}

bool OutsideCollapse::Hit(POINT point) const
{
    // The popup itself is checked by rectangle: it is a layered window with
    // rounded corners, where WindowFromPoint skips fully transparent pixels.
    RECT rect{};
    if (GetWindowRect(target_, &rect) && PtInRect(&rect, point)) return true;

    // Anything else of ours (the ball, the other popup, the host window) is not
    // an outside click: clicking the ball toggles the popup and must not also
    // collapse it.
    const HWND under = WindowFromPoint(point);
    if (!under) return false;
    const HWND root = GetAncestor(under, GA_ROOT);
    DWORD pid = 0;
    GetWindowThreadProcessId(root ? root : under, &pid);
    return pid == GetCurrentProcessId();
}

LRESULT CALLBACK OutsideCollapse::Proc(int code, WPARAM wParam, LPARAM lParam)
{
    // The hook sits in the system's input path, so the callback only hit-tests
    // and posts; the actual hide runs on the UI thread. Fan the click out to
    // every registered popup so a panel + usage list open together both react.
    if (code == HC_ACTION && ownerCount_ > 0 &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN))
    {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        for (int i = 0; i < ownerCount_; ++i)
        {
            OutsideCollapse* owner = owners_[i];
            if (!owner || !owner->target_ || !IsWindowVisible(owner->target_)) continue;
            if (!owner->Hit(info->pt)) PostMessageW(owner->target_, owner->message_, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
