#include "aggregate.h"

#include <algorithm>
#include <cwctype>

#pragma comment(lib, "psapi.lib")

namespace
{
struct Delta
{
    int64_t in = 0;
    int64_t out = 0;
    int64_t loopback = 0;
    int64_t external = 0;
};

int64_t Positive(int64_t value)
{
    return value > 0 ? value : 0;
}

Delta Subtract(const EtwCounters& before, const EtwCounters& now)
{
    const int64_t loopbackIn = Positive(now.loopbackIn - before.loopbackIn);
    const int64_t loopbackOut = Positive(now.loopbackOut - before.loopbackOut);
    const int64_t externalIn = Positive(now.externalIn - before.externalIn);
    const int64_t externalOut = Positive(now.externalOut - before.externalOut);

    Delta delta;
    delta.in = loopbackIn + externalIn;
    delta.out = loopbackOut + externalOut;
    delta.loopback = loopbackIn + loopbackOut;
    delta.external = externalIn + externalOut;
    return delta;
}

bool IsLocalProxy(const Delta& delta)
{
    return delta.loopback > ProcessAggregator::ProxyThresholdBytes &&
           delta.external > ProcessAggregator::ProxyThresholdBytes;
}

double Average(const std::deque<double>& values)
{
    if (values.empty()) return 0;
    double sum = 0;
    for (double value : values) sum += value;
    return sum / static_cast<double>(values.size());
}

void Push(std::deque<double>& values, double value)
{
    values.push_back(value);
    while (static_cast<int>(values.size()) > ProcessAggregator::WindowTicks) values.pop_front();
}

std::wstring ToLower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

std::wstring BaseName(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}
} // namespace

const ProcessAggregator::Identity& ProcessAggregator::ResolveIdentity(uint32_t pid,
                                                                      double nowSeconds)
{
    auto cached = identities_.find(pid);
    if (cached != identities_.end() && cached->second.expiresAt > nowSeconds)
    {
        return cached->second.identity;
    }

    Identity identity;
    bool resolved = false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
    {
        identity.key = L"pid:" + std::to_wstring(pid);
        identity.name = L"pid " + std::to_wstring(pid);
    }
    else
    {
        wchar_t path[1024]{};
        DWORD size = 1024;
        if (QueryFullProcessImageNameW(process, 0, path, &size))
        {
            const std::wstring full(path, size);
            identity.key = ToLower(full);
            identity.name = BaseName(full);
            resolved = true;
        }
        else
        {
            identity.key = L"pid:" + std::to_wstring(pid);
            identity.name = L"pid " + std::to_wstring(pid);
        }

        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(process, &created, &exited, &kernel, &user))
        {
            ULARGE_INTEGER ticks;
            ticks.LowPart = created.dwLowDateTime;
            ticks.HighPart = created.dwHighDateTime;
            identity.startTicks = static_cast<int64_t>(ticks.QuadPart);
        }

        CloseHandle(process);
    }

    if (!resolved && cached != identities_.end() && !cached->second.identity.key.starts_with(L"pid:"))
    {
        cached->second.expiresAt = nowSeconds + DeadIdentitySeconds;
        return cached->second.identity;
    }

    CachedIdentity entry;
    entry.identity = identity;
    entry.expiresAt = nowSeconds + 5;
    auto inserted = identities_.insert_or_assign(pid, entry);
    return inserted.first->second.identity;
}

void ProcessAggregator::Reset()
{
    entries_.clear();
    pidStart_.clear();
    previous_.clear();
    usageDeltas_.clear();
    lastTick_ = -1;
}

void ProcessAggregator::Prime(const std::vector<PidSnapshot>& snapshot, double nowSeconds)
{
    lastTick_ = nowSeconds;
    previous_.clear();
    for (const PidSnapshot& row : snapshot)
    {
        previous_[row.pid] = row.counters;
        pidStart_[row.pid] = ResolveIdentity(row.pid, nowSeconds).startTicks;
    }
}

std::vector<ProcessRow> ProcessAggregator::Update(const std::vector<PidSnapshot>& snapshot,
                                                  double nowSeconds,
                                                  double& externalBytesPerSecond)
{
    externalBytesPerSecond = 0;
    usageDeltas_.clear();

    if (lastTick_ < 0)
    {
        Prime(snapshot, nowSeconds);
        return {};
    }

    double seconds = nowSeconds - lastTick_;
    if (seconds > MaxGapSeconds)
    {
        Reset();
        Prime(snapshot, nowSeconds);
        return {};
    }

    lastTick_ = nowSeconds;
    if (seconds <= 0) seconds = 1;

    double externalTotal = 0;
    std::unordered_map<std::wstring, ProcessRow> tickSums;

    for (const PidSnapshot& row : snapshot)
    {
        const Identity& identity = ResolveIdentity(row.pid, nowSeconds);

        auto known = pidStart_.find(row.pid);
        if (known != pidStart_.end() && known->second != identity.startTicks)
        {
            // The PID now belongs to a different process: drop this tick so the
            // new process does not inherit the old one's bytes.
            known->second = identity.startTicks;
            continue;
        }
        pidStart_[row.pid] = identity.startTicks;

        EtwCounters before;
        auto previous = previous_.find(row.pid);
        if (previous != previous_.end()) before = previous->second;

        const Delta delta = Subtract(before, row.counters);
        externalTotal += static_cast<double>(delta.external);

        if (IsLocalProxy(delta)) continue;
        if (delta.in == 0 && delta.out == 0) continue;

        // Same bytes that feed the row, so the ledger and the panel agree.
        UsageDelta usage;
        usage.key = identity.key;
        usage.name = identity.name;
        usage.down = delta.in;
        usage.up = delta.out;
        usageDeltas_.push_back(std::move(usage));

        ProcessRow& sum = tickSums[identity.key];
        sum.name = identity.name;
        sum.down += static_cast<double>(delta.in) / seconds;
        sum.up += static_cast<double>(delta.out) / seconds;
    }

    for (auto& [key, sum] : tickSums)
    {
        Entry& entry = entries_[key];
        entry.name = sum.name;
        entry.lastDown = sum.down;
        entry.lastUp = sum.up;
        entry.lastSeen = nowSeconds;
        Push(entry.down, sum.down);
        Push(entry.up, sum.up);
    }

    for (auto& [key, entry] : entries_)
    {
        if (tickSums.find(key) != tickSums.end()) continue;

        // No bytes this tick: decay through the window instead of snapping to zero.
        Push(entry.down, 0);
        Push(entry.up, 0);
        entry.lastDown = 0;
        entry.lastUp = 0;
    }

    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (nowSeconds - it->second.lastSeen > RemoveAfterSeconds) it = entries_.erase(it);
        else ++it;
    }

    previous_.clear();
    for (const PidSnapshot& row : snapshot) previous_[row.pid] = row.counters;

    externalBytesPerSecond = externalTotal / seconds;
    return BuildRows();
}

std::vector<ProcessRow> ProcessAggregator::BuildRows() const
{
    struct Scored
    {
        const Entry* entry;
        double score;
        double total;
    };

    std::vector<Scored> rows;
    rows.reserve(entries_.size());

    for (const auto& [key, entry] : entries_)
    {
        const double score = Average(entry.down) + Average(entry.up);

        // Nothing in the whole window means the process stopped talking; hide it
        // now instead of leaving a 0/0 row on screen.
        if (score < MinVisibleBytesPerSecond) continue;

        rows.push_back({&entry, score, entry.lastDown + entry.lastUp});
    }

    double max = 1;
    for (const Scored& row : rows) max = std::max(max, row.score);

    std::sort(rows.begin(), rows.end(), [](const Scored& left, const Scored& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.total > right.total;
    });

    if (rows.size() > 5) rows.resize(5);

    std::vector<ProcessRow> result;
    result.reserve(rows.size());
    for (const Scored& row : rows)
    {
        ProcessRow out;
        out.name = row.entry->name;
        out.down = row.entry->lastDown;
        out.up = row.entry->lastUp;
        out.downText = FormatRate(out.down);
        out.upText = FormatRate(out.up);
        out.barWidth = BarMaxWidth * row.score / max;
        result.push_back(std::move(out));
    }

    return result;
}

std::wstring FormatRate(double bytesPerSecond)
{
    wchar_t buffer[32];

    if (bytesPerSecond >= 1024 * 1024)
        swprintf_s(buffer, L"%.1fM", bytesPerSecond / 1024 / 1024);
    else if (bytesPerSecond >= 1024)
        swprintf_s(buffer, L"%.0fK", bytesPerSecond / 1024);
    else
        swprintf_s(buffer, L"0");

    return buffer;
}
