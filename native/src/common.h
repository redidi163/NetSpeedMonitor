#pragma once

// Shared includes and small helpers for the native build.
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// winsock2.h has to precede windows.h; WIN32_LEAN_AND_MEAN keeps the old
// winsock.h out. objidl.h supplies the IStream/PROPID declarations that the
// lean windows.h omits and gdiplus.h requires. shellapi.h carries
// NOTIFYICONDATAW and CommandLineToArgvW.
#include <winsock2.h>
#include <windows.h>
#include <objidl.h>
#include <shellapi.h>

#include <cstdint>
#include <string>

// Monotonic clock, same role as Stopwatch in the C# build.
inline int64_t NowTicks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline double TicksToSeconds(int64_t delta)
{
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    return static_cast<double>(delta) / static_cast<double>(freq.QuadPart);
}

// "1.89" — the ball always speaks MB/s, two decimals.
std::wstring FormatMBps(double bytesPerSecond);
