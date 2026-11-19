#include "collapse.h"

namespace
{
// The popup opens mid-focus-handover (tray menu closing, --panel while the
// desktop settles); ignore the foreground poll until it has had time to land.
constexpr ULONGLONG kOpenGraceMs = 500;
} // namespace

OutsideCollapse* OutsideCollapse::owner_ = nullptr;

void OutsideCollapse::Start(HWND target, UINT message)
{
    if (!target) return;

    Stop(); // a second Start must not leak the previous hook

    target_ = target;
    message_ = message;
    foregroundAtOpen_ = GetForegroundWindow();
    openedAt_ = GetTickCount64();

    owner_ = this;
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, Proc, GetModuleHandleW(nullptr), 0);
}

void OutsideCollapse::Stop()
{
    if (hook_)
    {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    if (owner_ == this) owner_ = nullptr;
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
    // and posts; the actual hide runs on the UI thread.
    if (code == HC_ACTION && owner_ && owner_->target_ &&
        IsWindowVisible(owner_->target_) &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN))
    {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (!owner_->Hit(info->pt))
        {
            PostMessageW(owner_->target_, owner_->message_, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
