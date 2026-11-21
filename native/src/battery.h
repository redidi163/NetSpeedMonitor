#pragma once

#include "common.h"

#include <cstdio>
#include <optional>
#include <vector>

// Battery of the 2.4G receiver at A8A5:2255 ("Viper V3 Pro" / K-snake family).
//
// PROTOCOL (measured on-device 2026-11-20 / re-measured 2026-11-21):
// The receiver has three vendor collections. Only usage_page 0xFF01 (out=64,
// report id 0xF0) answers at all on interrupt OUT. The others (0xFFAB rid=0xAB,
// 0xFF03 rid=0x0A) never reply, and report id 0x00 is not declared by ANY
// interface -- writes to it are rejected by the HID stack (err=87).
//
//   request on FF01:  wire [F0] 55 30 A5 0B 2E 01 01 00 <zeros>  (64 bytes)
//   echo:             [F0] 55 30 <CHK> 0B 2E 01 01 00 <zeros>
//
// <CHK> IS NOT A BATTERY LEVEL. It is an additive checksum of the request: every
// other byte is echoed verbatim (marker bytes at [16..23] come back 8/8
// identical) and <CHK> moves by exactly the delta applied to any byte in
// payload[0..16]; changing payload[30] or [40] leaves it untouched. The old
// code read <CHK> as "60%" and later "59%" only because it sent [7]=0x01 vs
// 0x00 -- it was displaying our own frame's checksum. Parse() now returns
// nullopt for these echoes.
//
// The real reply -- AA 30 ... with percent@8 -- never arrived on this unit, even
// while the mouse was awake. Until it does, the capsule stays on "--".
//
// Ajazz frame (30 01) on the same 0xF0 interface returns a CONSTANT 0x64
// ("100%") regardless of the real level: also rejected.
//
// A second reply layout from other firmware revisions is still accepted:
//   reply AA 30 A5 0B ... ; percent@8 (1..100), charging@9.
//
// WRITE ENDPOINT: the receiver answers on the interrupt OUT pipe (WriteFile).
// HidD_SetOutputReport uses control SET_REPORT, which the class driver ACKs
// (err=0) but this firmware ignores; kept only as a fallback, logs tag the path.
//
// LENGTH: Windows OutputReportByteLength already includes the report id, so
// wire = [id]+(outLen-1); the FF01 collection (out=64) takes wire = [F0]+63 = 64B.
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
    // Parses by reply magic, not by report id: rejects the constant-100 Ajazz echo.
    static std::optional<BatteryReading> Parse(const std::vector<unsigned char>& raw);
    // writeVia: true = WriteFile on interrupt OUT (UMD/HidSharp path, preferred);
    //           false = HidD_SetOutputReport on control SET_REPORT (fallback).
    static bool Exchange(HANDLE device, unsigned char reportId,
                         const std::vector<unsigned char>& payload, int readLen, int timeoutMs,
                         bool writeViaInterrupt, std::vector<unsigned char>& reply, DWORD& error);

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

    // Wakes the worker so it re-reads the battery now instead of sleeping until
    // the next 6 h poll. Used after sleep/wake, where the receiver's state may
    // have changed while the machine was suspended. Safe to call before Start()
    // or after Stop().
    void Refresh();

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Loop();

    HANDLE thread_ = nullptr;
    HANDLE stop_ = nullptr;
    HANDLE wake_ = nullptr; // manual-reset: interrupts the long poll wait
    SRWLOCK lock_ = SRWLOCK_INIT;
    BatterySnapshot state_{};
    int64_t sampledTicks_ = 0;
};
