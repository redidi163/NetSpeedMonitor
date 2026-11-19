#pragma once

#include "aggregate.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Cumulative payload bytes per exe, for the usage statistics (native-11).
//
// The aggregator only keeps a 3 s window and deletes a row 5 s after its last
// byte, and EtwSampler's own counters are reset whenever the session is rebuilt
// (sleep/wake). Neither can serve as a long-term total, so every tick hands its
// post-attribution delta to this ledger, which never expires anything.
struct UsageRow
{
    std::wstring name;
    int64_t down = 0;
    int64_t up = 0;
};

class UsageLedger
{
public:
    void Add(const std::vector<UsageDelta>& deltas);
    void Reset();

    // Descending by down+up.
    std::vector<UsageRow> Top(size_t count) const;

    int64_t TotalDown() const { return totalDown_; }
    int64_t TotalUp() const { return totalUp_; }
    size_t Count() const { return accounts_.size(); }

private:
    struct Account
    {
        std::wstring name;
        int64_t down = 0;
        int64_t up = 0;
    };

    std::unordered_map<std::wstring, Account> accounts_;
    int64_t totalDown_ = 0;
    int64_t totalUp_ = 0;
};

// "0 B" / "12.3 KB" / "340.5 MB" / "1.2 GB"; binary units, one decimal above KB.
std::wstring FormatBytes(int64_t bytes);
