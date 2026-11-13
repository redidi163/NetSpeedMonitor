#pragma once

#include "common.h"

#include <cstdio>

#include <evntcons.h>
#include <evntrace.h>
#include <unordered_map>
#include <vector>

// Per-PID byte counters, split by segment. A local proxy logs every transfer
// twice (app -> proxy on loopback, proxy -> internet), so the two segments are
// kept apart and the attribution rules live in aggregate.cpp.
struct EtwCounters
{
    int64_t loopbackIn = 0;
    int64_t loopbackOut = 0;
    int64_t externalIn = 0;
    int64_t externalOut = 0;
};

struct PidSnapshot
{
    uint32_t pid = 0;
    EtwCounters counters;
};

// Native kernel-network ETW consumer: StartTrace + real-time consumer thread.
// Session name differs from the C# build so both can run side by side.
class EtwSampler
{
public:
    ~EtwSampler();

    bool Start(const std::wstring& sessionName);
    void Stop();

    bool Running() const { return running_; }
    const std::wstring& Error() const { return error_; }
    const std::wstring& EnableMode() const { return enableMode_; }

    int64_t EventsLost() const { return eventsLost_; }
    int64_t EventCount() const { return eventCount_; }
    int64_t TcpExternalIn() const { return tcpExternalIn_; }
    int64_t UdpExternalIn() const { return udpExternalIn_; }
    int64_t AllEvents() const { return allEvents_; }
    int64_t IdCount(int id) const { return (id >= 0 && id < 64) ? idCounts_[id] : 0; }
    int64_t TcpEvents() const { return tcpEvents_; }
    int64_t UdpEvents() const { return udpEvents_; }

    std::vector<PidSnapshot> Snapshot() const;

    // Diagnostic only: writes the first `limit` raw event payloads to `path`
    // as hex, so the MOF field offsets can be confirmed against real traffic.
    static void SetDump(const std::wstring& path, int limit);

private:
    static VOID WINAPI StaticCallback(PEVENT_RECORD record);
    static DWORD WINAPI ConsumerProc(LPVOID param);
    void OnEvent(PEVENT_RECORD record);

    TRACEHANDLE session_ = 0;
    TRACEHANDLE trace_ = 0;
    void* properties_ = nullptr;
    std::wstring sessionName_;
    std::wstring error_;
    std::wstring enableMode_;
    bool running_ = false;
    HANDLE thread_ = nullptr;

    mutable CRITICAL_SECTION lock_{};
    bool lockReady_ = false;
    std::unordered_map<uint32_t, EtwCounters> counters_;
    int64_t eventsLost_ = 0;
    int64_t eventCount_ = 0;
    int64_t tcpExternalIn_ = 0;
    int64_t udpExternalIn_ = 0;
    int64_t tcpEvents_ = 0;
    int64_t idCounts_[64] = {};
    int64_t allEvents_ = 0;
    int64_t udpEvents_ = 0;

    static EtwSampler* current_;
    static std::wstring dumpPath_;
    static FILE* dumpFile_;
    static int dumpLimit_;
};
