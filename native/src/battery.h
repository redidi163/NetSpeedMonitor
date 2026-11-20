#pragma once

#include "common.h"

#include <cstdio>
#include <optional>
#include <vector>

// Battery of the 2.4G receiver at A8A5:2255 ("Viper V3 Pro" / K-snake family).
//
// The receiver never volunteers the level: it answers a query written to one of
// its vendor collections. It is a YJX-CHIP (product string / firmware id), so the
// REAL battery command is the hardware-verified YJX/Ksnake frame (native-18):
//   request: report id 0x00, wire [00] 55 30 A5 0B 2E 01 01 01 <zeros>  (outLen bytes)
//   reply:   AA 30 A5 0B ... ; percent@8 (1..100), charging@9
// The Ajazz frame on report id 0xF0 (request 30 01) is a DIFFERENT device family
// (NJ07/NJ08) sharing the same VID; on this unit it returns a bogus 0x64 "100%".
// It is kept only as a last-resort fallback. See native-18 / protocol-yjx-a8a5.md.
//
// WRITE ENDPOINT (native-16): the receiver listens on the interrupt OUT pipe.
// UMD/HidSharp's OutputExchange uses stream.Write() -> WriteFile (interrupt OUT).
// HidD_SetOutputReport uses control SET_REPORT, which the class driver ACKs
// (err=0) but the device firmware ignores -> no reply on interrupt IN. Both
// endpoints are tried now; logs tag them INT-OUT / CTRL.
//
// LENGTH (native-18): Windows OutputReportByteLength already includes the report
// id, so wire = [id]+(outLen-1). The old "Ksnake needs 65B, can't fit out=64"
// assumption was wrong (HidSharp counts the payload, Windows counts the id).
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

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Loop();

    HANDLE thread_ = nullptr;
    HANDLE stop_ = nullptr;
    SRWLOCK lock_ = SRWLOCK_INIT;
    BatterySnapshot state_{};
    int64_t sampledTicks_ = 0;
};
