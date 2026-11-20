#pragma once

#include "common.h"

#include <cstdio>
#include <optional>
#include <vector>

// Battery of the 2.4G receiver at A8A5:2255 ("Viper V3 Pro" / K-snake family).
//
// The receiver never volunteers the level: it answers a query written to one of
// its vendor collections. Two wire layouts are known (handoff notes, 2026-11-19):
//   K-snake: report id 0x00, 64-byte payload starting 55 30 A5 0B 2E 01 01 00 00 00
//   Ajazz  : report id 0xF0, 63-byte payload starting 30 01
// This unit's ff01 collection accepts only id 0xF0 (id 0x00 is refused with
// ERROR_INVALID_PARAMETER), so every interface is probed with both ids and the
// first reply carrying a 1..100 percentage wins. See native-15.
struct BatteryReading
{
    int percent = 0;
    bool charging = false;
    std::wstring protocol; // L"ajazz" or L"ksnake"
    int outLen = 0;
};

class BatteryReader
{
public:
    // One query round, bounded by timeoutMs. Empty when no interface answered,
    // which is the normal result while the mouse sleeps.
    std::optional<BatteryReading> Read(int timeoutMs = 3000, FILE* log = nullptr);

    // True when a receiver with our VID:PID is enumerated at all - lets the UI
    // tell "no mouse" apart from "mouse asleep".
    bool DevicePresent();

    // Enumerates the vendor collections and writes one line per interface.
    void Describe(FILE* log);

    const std::wstring& LastError() const { return error_; }

private:
    struct Interface
    {
        std::wstring path;
        unsigned short vendorId = 0;
        unsigned short productId = 0;
        int inLen = 0;
        int outLen = 0;
    };

    std::vector<Interface> Enumerate();
    static std::optional<BatteryReading> Parse(const std::vector<unsigned char>& raw,
                                               unsigned char reportId, const wchar_t* protocol);
    static bool Exchange(HANDLE device, unsigned char reportId,
                         const std::vector<unsigned char>& payload, int readLen, int timeoutMs,
                         std::vector<unsigned char>& reply, DWORD& error);

    // The interface that answered last time goes first; a stale path simply fails
    // to open and the full sweep runs again.
    std::wstring cachedPath_;
    std::wstring error_;
};

// What the UI needs to know, refreshed by BatteryMonitor on a worker thread.
struct BatterySnapshot
{
    bool present = false; // a receiver with our VID:PID is enumerated
    bool valid = false;   // a reading newer than kSnapshotMaxAge
    int percent = 0;
    bool charging = false;
};

// Polls the receiver off the UI thread: one Read() can block for seconds and
// would otherwise stall the 1 s taskbar repaint.
class BatteryMonitor
{
public:
    void Start();
    void Stop();
    BatterySnapshot Snapshot();

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Loop();

    HANDLE thread_ = nullptr;
    HANDLE stop_ = nullptr;
    SRWLOCK lock_ = SRWLOCK_INIT;
    BatterySnapshot state_{};
    int64_t sampledTicks_ = 0;
};
