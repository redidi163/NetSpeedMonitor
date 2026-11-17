#pragma once

#include "aggregate.h"
#include "common.h"

#include <vector>

// 280x348 DIP panel: adapter name, down/up totals, 60 s sparkline, Top 5 rows.
// Layered window drawn with GDI+ so the rounded corners and text antialias the
// same way the WPF build does.
class PanelWindow
{
public:
    bool Create(HINSTANCE instance, int x, int y);

    HWND Hwnd() const { return hwnd_; }

    void MoveTo(int x, int y);
    // Adopts the DPI of the window the panel is attached to (the ball). A hidden
    // layered window reports 96 until it has been shown, so this is the only
    // reliable source at the moment the panel opens.
    void SetDpi(int dpi);
    void Show(bool visible);
    bool Toggle();
    bool IsVisible() const;

    // After a drag the panel reports its new top-left corner to `host` with
    // `message` (wParam = x, lParam = y) so the ball can follow it.
    void SetNotify(HWND host, UINT message);

    void SetData(const std::wstring& adapterName, double down, double up,
                 const std::vector<float>& trend, const std::vector<ProcessRow>& rows,
                 const std::wstring& footer);

    // Screen position of the top-left corner, in pixels.
    POINT Position() const;

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Render();
    bool InCloseButton(int x, int y) const;

    // Collapse-on-outside-click. The panel is WS_EX_NOACTIVATE, so it never
    // becomes foreground and WM_ACTIVATE cannot report an outside click. The
    // low-level mouse hook sees every button-down regardless of which window
    // ends up with focus, which is the only way to catch a click that leaves
    // the foreground window unchanged (clicking back into the app that was
    // already foreground when the panel opened).
    static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam);
    void InstallMouseHook();
    void RemoveMouseHook();
    bool IsOurWindowAt(POINT point) const;
    static PanelWindow* hookOwner_;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HWND host_ = nullptr;
    UINT notifyMessage_ = 0;
    int dpi_ = 96;
    bool visible_ = false;

    // Who was foreground when the panel opened: catches alt-tab and other
    // focus changes that never produce a mouse click.
    HWND foregroundAtOpen_ = nullptr;
    ULONGLONG openedAt_ = 0;
    HHOOK mouseHook_ = nullptr;

    bool dragging_ = false;
    bool dragged_ = false;
    bool closePressed_ = false;
    POINT dragStart_{};
    POINT dragOrigin_{};

    std::wstring adapterName_;
    std::wstring footer_;
    double down_ = 0;
    double up_ = 0;
    std::vector<float> trend_;
    std::vector<ProcessRow> rows_;
};
