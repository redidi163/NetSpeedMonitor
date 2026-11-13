#include "common.h"

#include <cstdio>

std::wstring FormatMBps(double bytesPerSecond)
{
    if (bytesPerSecond < 0) bytesPerSecond = 0;

    wchar_t buffer[32];
    swprintf_s(buffer, L"%.2f", bytesPerSecond / 1048576.0);
    return buffer;
}
