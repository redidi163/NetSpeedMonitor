#pragma once

#include "usage.h"

#include <cstdint>
#include <string>
#include <vector>

// One persisted row: what one exe moved on one day.
struct DayUsage
{
    std::wstring date; // YYYY-MM-DD
    std::wstring key;  // lowercased full image path, the ledger's key
    std::wstring name; // base name
    int64_t down = 0;
    int64_t up = 0;
    int64_t lastSeen = 0; // unix seconds
};

// Daily persistence for the usage ledger (native-12).
//
// Layout: <dir>\YYYY-MM.tsv, one file per month, tab separated, UTC dates are
// NOT used - dates come from the local clock so "today" matches the taskbar.
// The ledger stays the live source of truth for today: on startup the store
// seeds it with today's persisted rows, and every save rewrites today's rows
// from the ledger. That makes saves idempotent and keeps a crash from
// double-counting or losing more than the save interval.
class UsageStore
{
public:
    static constexpr int RetentionDays = 90;
    static constexpr int SaveIntervalSeconds = 30;
    static constexpr int64_t SaveDeltaBytes = 1024 * 1024;

    // Creates `dir` if needed and loads the months that still fall inside the
    // retention window. Returns false only when the directory cannot be listed;
    // a missing directory is not an error.
    bool Open(const std::wstring& dir, const std::wstring& today);

    // Adds today's persisted rows to the ledger. Call once, right after Open().
    void SeedToday(const std::wstring& today, UsageLedger& ledger) const;

    // Writes when `force`, or when 30 s / 1 MB have passed since the last save.
    bool MaybeSave(const std::wstring& today, const UsageLedger& ledger, double nowSeconds,
                   bool force);

    // Persisted days inside the window plus today's live ledger, descending.
    std::vector<UsageRow> Aggregate(int days, const std::wstring& today,
                                    const UsageLedger& ledger) const;

    const std::wstring& Dir() const { return dir_; }
    const std::wstring& Error() const { return error_; }
    int64_t Rows() const { return static_cast<int64_t>(rows_.size()); }

private:
    std::vector<DayUsage> LoadMonth(const std::wstring& path) const;
    bool WriteMonth(const std::wstring& today, const UsageLedger& ledger);

    std::wstring dir_;
    std::wstring error_;
    std::vector<DayUsage> rows_;
    double lastSavedAt_ = -1;
    int64_t lastSavedTotal_ = 0;
};

// "YYYY-MM-DD" for the local clock.
std::wstring LocalDateString();

// `<local app data>\NetSpeedMonitor\usage`.
std::wstring DefaultUsageDir();
