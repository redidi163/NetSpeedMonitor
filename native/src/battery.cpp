#include "battery.h"

#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace
{
constexpr unsigned short kProductId = 0x2255;
constexpr unsigned short kVendors[] = {0xA8A5, 0xA8A4};

// Only report ids the receiver actually declares. On-device probing showed the
// three vendor collections declare 0xF0 (FF01), 0xAB (FFAB) and 0x0A (FF03);
// 0x00 is declared by NONE, so writes there are rejected by the HID stack and
// never reach the firmware. FF01/0xF0 is the one that answers battery queries.
constexpr unsigned char kReportIds[] = {0xF0, 0xAB, 0x0A};

constexpr int kSettleMs = 25;   // device needs ~25 ms before the reply lands (UMD)
constexpr int kReplyMs = 400;   // per-frame read window (UMD OutputExchange timeout)
constexpr int kMaxAttempts = 8; // UMD OutputExchange retries

// Drain：读空输入缓冲的残留帧，超时后直接返回，忽略结果。
static void Drain(HANDLE device, int readLen, int timeoutMs)
{
    std::vector<unsigned char> buf(static_cast<size_t>(readLen), 0);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return;
    DWORD got = 0;
    if (!ReadFile(device, buf.data(), static_cast<DWORD>(readLen), &got, &ov))
    {
        if (GetLastError() == ERROR_IO_PENDING)
        {
            WaitForSingleObject(ov.hEvent, static_cast<DWORD>(timeoutMs));
            CancelIo(device);
        }
    }
    WaitForSingleObject(ov.hEvent, 50);
    CloseHandle(ov.hEvent);
}

constexpr int kPollOkMs = 21600000;    // healthy receiver: once every 6 hours
constexpr int kPollFailMs = 21600000;  // miss: retry in 6 hours
constexpr int kPollBackoffMs = 43200000; // three misses: back off to 12 hours (mouse asleep)
constexpr double kSnapshotMaxAgeSeconds = 25200.0; // 7 hours: keep snapshot valid across a 6 h poll

// How the total Read() budget is split between the two write endpoints. INTERRUPT
// gets the larger share because it is the one the firmware actually answers.
constexpr double kInterruptBudget = 0.65;

bool IsKnownVendor(unsigned short vendorId)
{
    for (unsigned short known : kVendors)
        if (vendorId == known) return true;
    return false;
}

// Send a numbered output report.
//
// viaInterrupt=true  -> WriteFile on the interrupt OUT pipe. This is what
//   UMD/HidSharp's OutputExchange does (stream.Write) and what this receiver's
//   firmware listens to. The handle is opened FILE_FLAG_OVERLAPPED, so writes
//   are completed through an OVERLAPPED event.
// viaInterrupt=false -> HidD_SetOutputReport on control SET_REPORT. The HID class
//   driver ACKs this (err=0) but the device ignores it -> kept only as a fallback.
//
// wire[0] is the report id; wire.size() == OutputReportByteLength (incl. id).
static bool WriteReport(HANDLE device, const std::vector<unsigned char>& wire,
                        bool viaInterrupt, DWORD& error)
{
    if (viaInterrupt)
    {
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) { error = GetLastError(); return false; }

        DWORD written = 0;
        BOOL ok = WriteFile(device, wire.data(), static_cast<DWORD>(wire.size()), &written, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(ov.hEvent, 1000) == WAIT_OBJECT_0 &&
                GetOverlappedResult(device, &ov, &written, FALSE))
                ok = TRUE;
        }
        CloseHandle(ov.hEvent);

        if (!ok) { error = GetLastError(); return false; }
        error = 0;
        return true;
    }

    // HidD_SetOutputReport takes PVOID (non-const) though it never writes the
    // buffer; wire is const here because both callers share one wire builder.
    if (!HidD_SetOutputReport(device, const_cast<unsigned char*>(wire.data()),
                              static_cast<ULONG>(wire.size())))
    {
        error = GetLastError();
        return false;
    }
    error = 0;
    return true;
}
} // namespace

std::vector<BatteryReader::Interface> BatteryReader::Enumerate()
{
    std::vector<Interface> found;

    GUID guid{};
    HidD_GetHidGuid(&guid);

    HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
    {
        error_ = L"SetupDiGetClassDevsW failed: " + std::to_wstring(GetLastError());
        return found;
    }

    SP_DEVICE_INTERFACE_DATA data{};
    data.cbSize = sizeof(data);

    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, index, &data); ++index)
    {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;

        std::vector<BYTE> buffer(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &data, detail, needed, nullptr, nullptr))
            continue;

        // Collection 0 is enough to read the VID/PID and the report lengths.
        HANDLE query = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
        if (query == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        const bool keep = HidD_GetAttributes(query, &attributes) != FALSE &&
                          attributes.ProductID == kProductId &&
                          IsKnownVendor(attributes.VendorID);

        Interface entry;
        if (keep)
        {
            entry.path = detail->DevicePath;
            entry.vendorId = attributes.VendorID;
            entry.productId = attributes.ProductID;

            PHIDP_PREPARSED_DATA preparsed = nullptr;
            if (HidD_GetPreparsedData(query, &preparsed))
            {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
                {
                    entry.inLen = caps.InputReportByteLength;
                    entry.outLen = caps.OutputReportByteLength;
                }
                HidD_FreePreparsedData(preparsed);
            }
        }

        CloseHandle(query);
        if (keep) found.push_back(std::move(entry));
    }

    SetupDiDestroyDeviceInfoList(set);

    // Widest output report first: that is the vendor channel on this family.
    std::sort(found.begin(), found.end(),
              [](const Interface& left, const Interface& right) { return left.outLen > right.outLen; });
    return found;
}

bool BatteryReader::Exchange(HANDLE device, unsigned char reportId,
                             const std::vector<unsigned char>& payload, int readLen,
                             int timeoutMs, bool writeViaInterrupt,
                             std::vector<unsigned char>& reply, DWORD& error)
{
    reply.clear();

    std::vector<unsigned char> wire;
    wire.reserve(payload.size() + 1);
    wire.push_back(reportId);
    wire.insert(wire.end(), payload.begin(), payload.end());

    // Write on the endpoint the firmware actually listens on. A wrong report id
    // still fails here without reaching the device, so the sweep stays cheap.
    if (!WriteReport(device, wire, writeViaInterrupt, error))
        return false;

    Sleep(kSettleMs);

    const int window = std::min(timeoutMs, kReplyMs);
    if (window <= 0) return false;

    reply.assign(static_cast<size_t>(readLen), 0);

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return false;

    DWORD got = 0;
    bool ok = false;

    if (ReadFile(device, reply.data(), static_cast<DWORD>(readLen), &got, &overlapped))
    {
        ok = got > 0;
    }
    else if (GetLastError() == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(overlapped.hEvent, static_cast<DWORD>(window)) == WAIT_OBJECT_0 &&
            GetOverlappedResult(device, &overlapped, &got, FALSE))
        {
            ok = got > 0;
        }
        else
        {
            CancelIo(device);
            WaitForSingleObject(overlapped.hEvent, 50);
        }
    }

    CloseHandle(overlapped.hEvent);

    if (!ok)
    {
        reply.clear();
        return false;
    }

    reply.resize(got);
    return true;
}

std::optional<BatteryReading> BatteryReader::Parse(const std::vector<unsigned char>& raw)
{
    if (raw.size() < 5) return std::nullopt;

    // raw includes the leading report id (e.g. 0xF0). Strip it if present.
    std::vector<unsigned char> body = raw;
    if (body.size() > 1 && (body[0] == 0xF0 || body[0] == 0xAB || body[0] == 0x0A))
        body.erase(body.begin());

    // NO battery layout lives on a "55 30 ..." reply — that frame is an ACK echo.
    // Measured 2026-11-21 (probe_echo.py / probe_delta.py):
    //   * every payload byte comes back byte-identical (8/8 marker bytes at
    //     [16..23] mirrored), so the receiver just echoes what we wrote;
    //   * body[2] is a plain additive checksum over payload[0..16] minus [2]
    //     (+0x7B): changing [5],[6],[7],[16] moves it by exactly the same
    //     delta, changing [30],[40] does not move it at all;
    //   * it therefore "tracks" our own frame, not the mouse: [7] 0x01 -> 0x3C
    //     and [7] 0x00 -> 0x3B is why the old code looked like it read 60% and
    //     then 59%. Reading it as a level was a false positive.
    // The real reply (AA 30 ... pct@8) never arrives, so report nothing and let
    // Read() retry rather than display a checksum as a battery level.
    if (body.size() >= 5 && body[0] == 0x55 && body[1] == 0x30) return std::nullopt;

    // Layout B — UMD/AreYouLowBattery YJX variant: AA 30 A5 0B ... pct@8 chg@9.
    if (body.size() >= 10 && body[0] == 0xAA && body[1] == 0x30)
    {
        const int pct = body[8];
        if (pct >= 1 && pct <= 100)
            return BatteryReading{pct, body[9] != 0, L"ksnake-yjx"};
    }

    // Ajazz frame (30 01 ... 64) is deliberately NOT parsed: it returns a
    // constant 0x64 "100%" regardless of charge. Reporting it was the original
    // bug. Returning nullopt makes Read() keep trying instead of showing 100%.
    return std::nullopt;
}

std::optional<BatteryReading> BatteryReader::Read(int timeoutMs, FILE* log)
{
    const ULONGLONG start = GetTickCount64();
    const ULONGLONG deadline = start + static_cast<ULONGLONG>(timeoutMs);
    // interrupt OUT (firmware path) gets the first slice; control is a fallback.
    const ULONGLONG split = start +
        static_cast<ULONGLONG>(static_cast<double>(timeoutMs) * kInterruptBudget);

    std::vector<Interface> interfaces = Enumerate();
    if (interfaces.empty())
    {
        error_ = L"no A8A5:2255 interface";
        if (log) fwprintf(log, L"# no interface\n");
        return std::nullopt;
    }

    if (!cachedPath_.empty())
    {
        std::stable_sort(interfaces.begin(), interfaces.end(),
                         [this](const Interface& left, const Interface& right)
                         {
                             const bool leftCached = left.path == cachedPath_;
                             const bool rightCached = right.path == cachedPath_;
                             return leftCached && !rightCached;
                         });
    }

    // Two write endpoints: interrupt OUT first (what the firmware answers), then
    // control SET_REPORT. Each pass has its own deadline so a silent interrupt
    // pipe cannot starve the control fallback - and the log shows which one worked.
    for (int endpoint = 0; endpoint < 2; ++endpoint)
    {
        const bool viaInterrupt = endpoint == 0;
        const ULONGLONG passDeadline = viaInterrupt ? split : deadline;
        if (GetTickCount64() >= passDeadline) continue;

        if (log)
            fwprintf(log, L"# === write via %s ===\n", viaInterrupt ? L"INT-OUT" : L"CTRL");

        for (const Interface& entry : interfaces)
        {
            if (entry.outLen < 2) continue;
            if (GetTickCount64() >= passDeadline) break;

            HANDLE device = CreateFileW(entry.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OVERLAPPED, nullptr);
            if (device == INVALID_HANDLE_VALUE)
            {
                if (log) fwprintf(log, L"# out=%d open failed err=%lu\n", entry.outLen, GetLastError());
                continue;
            }

            const int readLen = std::max(entry.inLen, entry.outLen);
            std::optional<BatteryReading> hit;

            for (unsigned char reportId : kReportIds)
            {
                if (GetTickCount64() >= passDeadline) break;

                // OutputReportByteLength on Windows already counts the leading
                // report id, so payload = outLen-1 and wire = [id]+payload = outLen.
                // The FF01 collection (out=64) takes wire = [F0]+63 = 64B.
                if (entry.outLen < 9) continue;  // room for the 8-byte Ksnake head

                std::vector<unsigned char> payload(static_cast<size_t>(entry.outLen) - 1, 0);

                // Ksnake/YJX GET_BATTERY on every declared report id. On the
                // FF01/0xF0 collection this returns 55 30 <PCT> 0B 2E ... and
                // Parse() reads the live level from body[2]. The Ajazz 30 01
                // frame returned a constant fake 100% and is no longer sent.
                static const unsigned char kHead[] = {0x55, 0x30, 0xA5, 0x0B, 0x2E,
                                                      0x01, 0x01, 0x01};
                const size_t copy = std::min(payload.size(), sizeof(kHead));
                std::memcpy(payload.data(), kHead, copy);

                // UMD OutputExchange 时序：Drain → write → sleep(25ms) → ReadFile(400ms)，
                // 最多 8 次尝试。每次失败后间隔 30ms 再重试。
                for (int attempt = 0; attempt < kMaxAttempts; ++attempt)
                {
                    if (GetTickCount64() >= passDeadline) break;

                    // 先把输入缓冲的残留帧读空
                    Drain(device, readLen, 20);

                    std::vector<unsigned char> reply;
                    DWORD error = 0;
                    const ULONGLONG now = GetTickCount64();
                    const int window = now >= passDeadline ? 0
                                       : static_cast<int>(std::min(passDeadline, deadline) - now);
                    if (window <= 0) break;

                    if (!Exchange(device, reportId, payload, readLen, window,
                                  viaInterrupt, reply, error))
                    {
                        if (log)
                            fwprintf(log, L"# out=%d rid=%02X %s attempt=%d no reply err=%lu\n",
                                     entry.outLen, reportId, viaInterrupt ? L"INT-OUT" : L"CTRL",
                                     attempt, error);
                        Sleep(30);
                        continue;
                    }

                    if (log)
                    {
                        std::wstring hex;
                        for (unsigned char byte : reply)
                        {
                            wchar_t cell[8];
                            swprintf_s(cell, L"%02X ", byte);
                            hex += cell;
                        }
                        fwprintf(log, L"# out=%d rid=%02X %s attempt=%d rx %ls\n",
                                 entry.outLen, reportId, viaInterrupt ? L"INT-OUT" : L"CTRL",
                                 attempt, hex.c_str());
                    }

                    hit = Parse(reply);
                    if (hit)
                    {
                        hit->outLen = entry.outLen;
                        cachedPath_ = entry.path;
                        break;
                    }
                    Sleep(30);
                }

                if (hit) break;
            }

            CloseHandle(device);
            if (hit) return hit;
        }
    }

    error_ = L"no reply";
    if (log) fwprintf(log, L"# no reply\n");
    return std::nullopt;
}

bool BatteryReader::DevicePresent()
{
    return !Enumerate().empty();
}

void BatteryReader::Describe(FILE* log)
{
    if (!log) return;

    const std::vector<Interface> interfaces = Enumerate();
    fwprintf(log, L"# interfaces=%d\n", static_cast<int>(interfaces.size()));
    for (const Interface& entry : interfaces)
    {
        fwprintf(log, L"I out=%d in=%d vid=%04X pid=%04X %ls\n", entry.outLen, entry.inLen,
                 entry.vendorId, entry.productId, entry.path.c_str());
    }
    fflush(log);
}


void BatteryMonitor::Start()
{
    if (thread_) return;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_ || !wake_)
    {
        if (stop_) { CloseHandle(stop_); stop_ = nullptr; }
        if (wake_) { CloseHandle(wake_); wake_ = nullptr; }
        return;
    }
    thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
}

void BatteryMonitor::Stop()
{
    if (stop_) SetEvent(stop_);
    if (wake_) SetEvent(wake_); // release a wait in case stop_ alone is missed
    if (thread_)
    {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_)
    {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
    if (wake_)
    {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
}

void BatteryMonitor::Refresh()
{
    // Manual-reset event: the worker clears it after waking. Calling this
    // before Start() or after Stop() is a harmless no-op.
    if (wake_) SetEvent(wake_);
}

DWORD WINAPI BatteryMonitor::ThreadProc(void* self)
{
    static_cast<BatteryMonitor*>(self)->Loop();
    return 0;
}

void BatteryMonitor::Loop()
{
    BatteryReader reader;
    int failures = 0;

    for (;;)
    {
        const std::optional<BatteryReading> reading = reader.Read(2500);
        const bool present = reader.DevicePresent();

        {
            AcquireSRWLockExclusive(&lock_);
            state_.present = present;
            state_.valid = reading.has_value();
            if (reading)
            {
                state_.percent = reading->percent;
                state_.charging = reading->charging;
                sampledTicks_ = NowTicks();
            }
            ReleaseSRWLockExclusive(&lock_);
        }

        failures = reading ? 0 : failures + 1;
        const int wait = reading ? kPollOkMs : (failures >= 3 ? kPollBackoffMs : kPollFailMs);

        // Sleep until the next poll, unless Stop() or a wake request (e.g. after
        // resume) interrupts. A woken pass re-reads immediately and keeps the
        // existing failure counter, so a transient asleep mouse still backs off.
        HANDLE waits[] = {stop_, wake_};
        const DWORD signaled =
            WaitForMultipleObjects(2, waits, FALSE, static_cast<DWORD>(wait));
        if (signaled == WAIT_OBJECT_0) break; // stop_
        if (signaled == WAIT_OBJECT_0 + 1) ResetEvent(wake_); // wake_: read again now
    }
}

BatterySnapshot BatteryMonitor::Snapshot()
{
    AcquireSRWLockShared(&lock_);
    BatterySnapshot copy = state_;
    const int64_t sampled = sampledTicks_;
    ReleaseSRWLockShared(&lock_);

    if (sampled != 0 && TicksToSeconds(NowTicks() - sampled) > kSnapshotMaxAgeSeconds)
        copy.valid = false;
    return copy;
}
