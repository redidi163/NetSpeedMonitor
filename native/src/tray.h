#pragma once

#include "common.h"

// Shell_NotifyIcon wrapper. The owner window receives the callback message.
class TrayIcon
{
public:
    ~TrayIcon();

    bool Create(HINSTANCE instance, HWND owner, UINT callbackMessage);
    void SetTooltip(const std::wstring& text);
    void Remove();

private:
    NOTIFYICONDATAW data_{};
    HICON icon_ = nullptr;
    bool added_ = false;
};
