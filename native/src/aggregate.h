#pragma once

#include "common.h"
#include "etw.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ProcessRow
{
    std::wstring name;
    double down = 0;
    double up = 0;
    std::wstring downText;
    std::wstring upText;
    double barWidth = 0;
};

// One tick's contribution for a single exe, measured after the local-proxy
// filter so a long-term ledger built from these deltas uses exactly the same
// attribution as the on-screen rows.
struct UsageDelta
{
    std::wstring key;  // lowercased full image path
    std::wstring name; // base name
    int64_t down = 0;
    int64_t up = 0;
};

// Port of Services/ProcessAggregator.cs. Three rules live here:
// - rows merge by exe path, so three instances of one exe take a single row;
// - ordering uses a 3 tick average while the numbers stay instantaneous;
// - a process keeps its row for up to 5 s after its last byte, decaying
//   through the average instead of vanishing.
class ProcessAggregator
{
public:
    static constexpr int WindowTicks = 3;
    static constexpr double RemoveAfterSeconds = 5;
    static constexpr double MinVisibleBytesPerSecond = 1024;
    static constexpr double MaxGapSeconds = 5;

    // A dead process keeps the identity it had while alive: ETW events for it
    // can still be drained after it exits, and naming them pid:N would split a
    // single process across two accounts (seen with curl: 7.5 MB + 5.1 MB).
    static constexpr double DeadIdentitySeconds = 30;

    // A process that moves more than this in both the loopback and the external
    // segment in one window is a local proxy; its counters are dropped so its
    // downstream clients are not counted twice.
    static constexpr int64_t ProxyThresholdBytes = 8192;

    static constexpr double BarMaxWidth = 120;

    // Returns the Top 5 and writes the external-segment total rate.
    std::vector<ProcessRow> Update(const std::vector<PidSnapshot>& snapshot, double nowSeconds,
                                   double& externalBytesPerSecond);

    // Deltas produced by the most recent Update(), for the cumulative ledger.
    // Taken rather than read: a caller that forgets to consume would otherwise
    // add the same bytes twice on the next tick.
    std::vector<UsageDelta> TakeUsageDeltas() { return std::move(usageDeltas_); }

    void Reset();

private:
    struct Identity
    {
        std::wstring key;
        std::wstring name;
        int64_t startTicks = 0;
    };

    struct Entry
    {
        std::wstring name;
        std::deque<double> down;
        std::deque<double> up;
        double lastDown = 0;
        double lastUp = 0;
        double lastSeen = 0;
    };

    struct CachedIdentity
    {
        Identity identity;
        double expiresAt = 0;
    };

    const Identity& ResolveIdentity(uint32_t pid, double nowSeconds);
    void Prime(const std::vector<PidSnapshot>& snapshot, double nowSeconds);
    std::vector<ProcessRow> BuildRows() const;

    std::unordered_map<uint32_t, CachedIdentity> identities_;
    std::unordered_map<std::wstring, Entry> entries_;
    std::unordered_map<uint32_t, int64_t> pidStart_;
    std::unordered_map<uint32_t, EtwCounters> previous_;
    std::vector<UsageDelta> usageDeltas_;
    double lastTick_ = -1;
};
