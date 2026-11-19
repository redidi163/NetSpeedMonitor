#include "usage.h"

#include <algorithm>
#include <cstdio>

void UsageLedger::Add(const std::vector<UsageDelta>& deltas)
{
    for (const UsageDelta& delta : deltas)
    {
        // Negative deltas cannot happen (they are clamped in the aggregator), but
        // a ledger that can go backwards would silently corrupt the totals.
        const int64_t down = delta.down > 0 ? delta.down : 0;
        const int64_t up = delta.up > 0 ? delta.up : 0;
        if (down == 0 && up == 0) continue;

        Account& account = accounts_[delta.key];
        account.name = delta.name;
        account.down += down;
        account.up += up;

        totalDown_ += down;
        totalUp_ += up;
    }
}

void UsageLedger::Reset()
{
    accounts_.clear();
    totalDown_ = 0;
    totalUp_ = 0;
}

std::vector<UsageRow> UsageLedger::Top(size_t count) const
{
    std::vector<UsageRow> rows;
    rows.reserve(accounts_.size());

    for (const auto& [key, account] : accounts_)
    {
        UsageRow row;
        row.name = account.name;
        row.down = account.down;
        row.up = account.up;
        rows.push_back(std::move(row));
    }

    std::sort(rows.begin(), rows.end(), [](const UsageRow& left, const UsageRow& right) {
        const int64_t leftTotal = left.down + left.up;
        const int64_t rightTotal = right.down + right.up;
        if (leftTotal != rightTotal) return leftTotal > rightTotal;
        return left.down > right.down;
    });

    if (rows.size() > count) rows.resize(count);
    return rows;
}

std::wstring FormatBytes(int64_t bytes)
{
    if (bytes < 0) bytes = 0;

    wchar_t buffer[32];
    if (bytes >= 1024LL * 1024 * 1024)
        swprintf_s(buffer, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024LL * 1024)
        swprintf_s(buffer, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    else
        swprintf_s(buffer, L"%lld B", static_cast<long long>(bytes));

    return buffer;
}
