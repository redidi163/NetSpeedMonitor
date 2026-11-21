#include "etw.h"

#include <evntcons.h>
#include <evntrace.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "tdh.lib")

namespace
{
// Microsoft-Windows-Kernel-Network. Event ids 10/11 are TCP send/recv,
// 26/27 are UDP send/recv; the C# build subscribes to the same four.
const GUID kKernelNetwork =
    {0x7dd42a49, 0x5329, 0x4832, {0x8d, 0xfd, 0x43, 0xd9, 0x79, 0x15, 0x3a, 0x88}};

// Stable session GUID so a stale session from a killed instance is easy to spot.
const GUID kSessionGuid =
    {0x9b2f1c40, 0x5a61, 0x4d0e, {0x9c, 0x11, 0x6f, 0x2a, 0x7e, 0x33, 0x51, 0x04}};

constexpr uint16_t kTcpSend = 10;
constexpr uint16_t kTcpRecv = 11;
constexpr uint16_t kUdpSend = 26;
constexpr uint16_t kUdpRecv = 27;

struct Payload
{
    uint32_t pid;
    uint32_t size;
    uint32_t daddr;
    uint32_t saddr;
};

bool ReadPayload(PEVENT_RECORD record, Payload& payload)
{
    if (!record->UserData || record->UserDataLength < sizeof(payload)) return false;
    memcpy(&payload, record->UserData, sizeof(payload));
    return true;
}

bool IsLoopback(uint32_t address)
{
    // 127.0.0.0/8 in network byte order.
    return (address & 0xFF) == 127;
}

// A killed instance leaves its session (and the kernel buffers it reserved)
// behind; a few of those exhaust the pool and every later start fails with
// ERROR_NO_SYSTEM_RESOURCES. The name is fixed, so stopping it by name is
// enough.
void StopStaleSession(const std::wstring& sessionName)
{
    const ULONG size =
        sizeof(EVENT_TRACE_PROPERTIES) + static_cast<ULONG>((sessionName.size() + 1) * sizeof(wchar_t));
    std::vector<BYTE> buffer(size, 0);

    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
    props->Wnode.BufferSize = size;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ControlTraceW(0, sessionName.c_str(), props, EVENT_TRACE_CONTROL_STOP);
}
} // namespace

EtwSampler* EtwSampler::current_ = nullptr;
std::wstring EtwSampler::dumpPath_;
FILE* EtwSampler::dumpFile_ = nullptr;
int EtwSampler::dumpLimit_ = 0;

EtwSampler::~EtwSampler()
{
    Stop();
    if (lockReady_) DeleteCriticalSection(&lock_);
}

void EtwSampler::SetDump(const std::wstring& path, int limit)
{
    // The file is opened lazily on the consumer thread; holding it open from
    // the main thread makes the append used by the summary fail.
    dumpPath_ = path;
    dumpLimit_ = limit;
}

bool EtwSampler::Start(const std::wstring& sessionName)
{
    if (running_) return true;

    // Idempotent cleanup: Stop() is a no-op on a fresh object and destroys any
    // critical section left by a previous run, so the Init below always pairs
    // with exactly one Delete. A sleep/wake rebuild goes Stop() -> Start() and
    // must not re-initialise a still-live lock_ (undefined behaviour + leak).
    Stop();

    sessionName_ = sessionName;
    InitializeCriticalSection(&lock_);
    lockReady_ = true;
    current_ = this;

    StopStaleSession(sessionName);

    const size_t nameBytes = (sessionName.size() + 1) * sizeof(wchar_t);
    const size_t size = sizeof(EVENT_TRACE_PROPERTIES) + nameBytes;
    auto* props = static_cast<EVENT_TRACE_PROPERTIES*>(calloc(1, size));
    if (!props)
    {
        error_ = L"calloc failed";
        return false;
    }
    properties_ = props;

    props->Wnode.BufferSize = static_cast<ULONG>(size);
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->Wnode.Guid = kSessionGuid;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    // Real-time kernel-network events arrive in bursts; a 2 MB pool (64 KB x 32)
    // silently loses most of a 20 MB/s download.
    // The real-time consumer allocates its own copy of the buffer pool, so this
    // also sets the process' private bytes: 128 KB x 32 keeps it near 4 MB.
    props->BufferSize = 128; // KB per buffer
    props->MinimumBuffers = 4;
    props->MaximumBuffers = 32;
    props->FlushTimer = 1;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    memcpy(reinterpret_cast<BYTE*>(props) + sizeof(EVENT_TRACE_PROPERTIES),
           sessionName.c_str(), nameBytes);

    ULONG result = StartTraceW(&session_, sessionName.c_str(), props);
    if (result != ERROR_SUCCESS)
    {
        error_ = L"StartTrace " + std::to_wstring(result);
        return false;
    }

    // Since Windows 8 a named session can enable the kernel-network provider
    // directly; the legacy EnableFlags path is the fallback.
    result = EnableTraceEx2(session_, &kKernelNetwork, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                            TRACE_LEVEL_INFORMATION, 0, 0, 0, nullptr);
    if (result == ERROR_SUCCESS)
    {
        enableMode_ = L"EnableTraceEx2";
    }
    else
    {
        const ULONG ex2Result = result;
        props->EnableFlags = EVENT_TRACE_FLAG_NETWORK_TCPIP;
        result = ControlTraceW(session_, sessionName.c_str(), props, EVENT_TRACE_CONTROL_UPDATE);
        if (result == ERROR_SUCCESS)
        {
            enableMode_ = L"EnableFlags";
        }
        else
        {
            error_ = L"ex2 " + std::to_wstring(ex2Result) + L", flags " + std::to_wstring(result);
            Stop();
            return false;
        }
    }

    thread_ = CreateThread(nullptr, 0, &EtwSampler::ConsumerProc, this, 0, nullptr);

    running_ = thread_ != nullptr;
    if (!running_) error_ = L"CreateThread failed";
    return running_;
}

DWORD WINAPI EtwSampler::ConsumerProc(LPVOID param)
{
    auto* self = static_cast<EtwSampler*>(param);

    EVENT_TRACE_LOGFILEW file{};
    file.LoggerName = const_cast<LPWSTR>(self->sessionName_.c_str());
    file.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    file.EventRecordCallback = &EtwSampler::StaticCallback;

    self->trace_ = OpenTraceW(&file);
    if (self->trace_ == INVALID_PROCESSTRACE_HANDLE)
    {
        self->error_ = L"OpenTrace " + std::to_wstring(GetLastError());
        self->trace_ = 0;
        return 0;
    }

    ProcessTrace(&self->trace_, 1, nullptr, nullptr);
    return 0;
}

void EtwSampler::Stop()
{
    if (trace_)
    {
        CloseTrace(trace_);
        trace_ = 0;
    }

    if (session_)
    {
        auto* props = static_cast<EVENT_TRACE_PROPERTIES*>(properties_);
        if (props)
        {
            ControlTraceW(session_, nullptr, props, EVENT_TRACE_CONTROL_STOP);
            eventsLost_ = props->EventsLost;
        }
        session_ = 0;
    }

    if (thread_)
    {
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }

    // Destroy the lock only after the consumer thread that used it has exited.
    // This pairs with Start()'s InitializeCriticalSection so wake rebuilds do
    // not accumulate un-deleted critical sections.
    if (lockReady_)
    {
        DeleteCriticalSection(&lock_);
        lockReady_ = false;
    }

    free(properties_);
    properties_ = nullptr;
    running_ = false;
    current_ = nullptr;

    if (dumpFile_)
    {
        fclose(dumpFile_);
        dumpFile_ = nullptr;
    }
}

VOID WINAPI EtwSampler::StaticCallback(PEVENT_RECORD record)
{
    if (current_) current_->OnEvent(record);
}

void EtwSampler::OnEvent(PEVENT_RECORD record)
{
    const EVENT_HEADER& header = record->EventHeader;
    ++allEvents_;
    if (!IsEqualGUID(header.ProviderId, kKernelNetwork)) return;

    const uint16_t id = header.EventDescriptor.Id;
    if (id < 64) ++idCounts_[id];

    if (dumpLimit_ > 0)
    {
        if (!dumpFile_) _wfopen_s(&dumpFile_, dumpPath_.c_str(), L"a");
        if (dumpFile_)
        {
            fwprintf(dumpFile_, L"id=%u len=%u:", id, record->UserDataLength);
            const auto* bytes = static_cast<const BYTE*>(record->UserData);
            for (uint16_t i = 0; i < record->UserDataLength && i < 32; ++i)
                fwprintf(dumpFile_, L" %02x", bytes[i]);
            fwprintf(dumpFile_, L"\n");
            fflush(dumpFile_);

            if (--dumpLimit_ == 0)
            {
                fclose(dumpFile_);
                dumpFile_ = nullptr;
            }
        }
    }

    if (id != kTcpSend && id != kTcpRecv && id != kUdpSend && id != kUdpRecv) return;

    Payload payload{};
    if (!ReadPayload(record, payload)) return;
    if (payload.size == 0) return;

    const bool loopback = IsLoopback(payload.saddr) || IsLoopback(payload.daddr);
    const bool sent = (id == kTcpSend || id == kUdpSend);

    EnterCriticalSection(&lock_);
    const bool tcp = (id == kTcpSend || id == kTcpRecv);
    if (tcp) ++tcpEvents_;
    else ++udpEvents_;

    EtwCounters& counters = counters_[payload.pid];
    if (loopback)
    {
        if (sent) counters.loopbackOut += payload.size;
        else counters.loopbackIn += payload.size;
    }
    else
    {
        if (sent) counters.externalOut += payload.size;
        else
        {
            counters.externalIn += payload.size;
            if (tcp) tcpExternalIn_ += payload.size;
            else udpExternalIn_ += payload.size;
        }
    }
    ++eventCount_;
    LeaveCriticalSection(&lock_);
}

std::vector<PidSnapshot> EtwSampler::Snapshot() const
{
    std::vector<PidSnapshot> rows;
    if (!lockReady_) return rows;

    EnterCriticalSection(&lock_);
    rows.reserve(counters_.size());
    for (const auto& [pid, counters] : counters_)
    {
        rows.push_back({pid, counters});
    }
    LeaveCriticalSection(&lock_);

    return rows;
}
