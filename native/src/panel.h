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

    void SetData(const std::wstring& adapterName, double down, double up,
                 const std::vector<float>& trend, const std::vector<ProcessRow>& rows,
                 const std::wstring& footer);

    // Screen position of the top-left corner, in pixels.
    POINT Position() const;

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Render();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    int dpi_ = 96;
    bool visible_ = false;

    std::wstring adapterName_;
    std::wstring footer_;
    double down_ = 0;
    double up_ = 0;
    std::vector<float> trend_;
    std::vector<ProcessRow> rows_;
};
