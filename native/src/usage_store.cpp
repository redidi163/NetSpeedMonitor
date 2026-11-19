#include "usage_store.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
constexpr size_t kLineChars = 8192;

// "YYYY-MM-DD" -> days since the epoch; -1 when the text is not a real date.
int64_t Ordinal(const std::wstring& date)
{
    int year = 0;
    int month = 0;
    int day = 0;
    if (swscanf_s(date.c_str(), L"%d-%d-%d", &year, &month, &day) != 3) return -1;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return -1;

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    const std::tm original = tm;

    const std::time_t seconds = _mkgmtime(&tm);
    if (seconds < 0) return -1;
    // _mkgmtime normalises out-of-range fields in place; a mismatch means the
    // input was not a date (2026-02-31 would become 2026-03-03).
    if (tm.tm_year != original.tm_year || tm.tm_mon != original.tm_mon ||
        tm.tm_mday != original.tm_mday)
    {
        return -1;
    }

    return static_cast<int64_t>(seconds) / 86400;
}

std::wstring Sanitize(const std::wstring& value)
{
    std::wstring out = value;
    for (wchar_t& c : out)
    {
        if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    }
    return out;
}

std::vector<std::wstring> Fields(const std::wstring& line)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (true)
    {
        const size_t tab = line.find(L'\t', start);
        if (tab == std::wstring::npos)
        {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return parts;
}

int64_t ToInt64(const std::wstring& text)
{
    wchar_t* end = nullptr;
    const long long value = wcstoll(text.c_str(), &end, 10);
    if (end == text.c_str()) return 0;
    return value > 0 ? static_cast<int64_t>(value) : 0;
}
} // namespace

std::wstring LocalDateString()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t buffer[16];
    swprintf_s(buffer, L"%04u-%02u-%02u", now.wYear, now.wMonth, now.wDay);
    return buffer;
}

std::wstring DefaultUsageDir()
{
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base)
    {
        dir = std::wstring(base) + L"\\NetSpeedMonitor\\usage";
    }
    else
    {
        dir = L"NetSpeedMonitor\\usage"; // last resort: current directory
    }
    if (base) CoTaskMemFree(base);
    return dir;
}

bool UsageStore::Open(const std::wstring& dir, const std::wstring& today)
{
    dir_ = dir;
    error_.clear();
    rows_.clear();

    const int64_t todayOrdinal = Ordinal(today);
    if (todayOrdinal < 0)
    {
        error_ = L"bad date: " + today;
        return false;
    }
    const int64_t cutoff = todayOrdinal - RetentionDays;

    const int created = SHCreateDirectoryExW(nullptr, dir_.c_str(), nullptr);
    if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS && created != ERROR_FILE_EXISTS)
    {
        error_ = L"mkdir failed: " + std::to_wstring(created);
        return false;
    }

    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((dir_ + L"\\*.tsv").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE)
    {
        // An empty directory is the normal first run, not an error.
        return true;
    }

    do
    {
        const std::wstring name = found.cFileName;
        if (name.size() != 11) continue; // YYYY-MM.tsv

        const std::wstring monthStart = name.substr(0, 7) + L"-01";
        const int64_t monthOrdinal = Ordinal(monthStart);
        if (monthOrdinal < 0) continue;

        const std::wstring path = dir_ + L"\\" + name;
        if (monthOrdinal + 31 < cutoff)
        {
            // The whole month is outside the window: drop the file instead of
            // leaving dead bytes behind (native-12 判据 5).
            DeleteFileW(path.c_str());
            continue;
        }

        for (DayUsage& row : LoadMonth(path))
        {
            if (Ordinal(row.date) < cutoff) continue;
            rows_.push_back(std::move(row));
        }
    } while (FindNextFileW(find, &found));

    FindClose(find);
    return true;
}

std::vector<DayUsage> UsageStore::LoadMonth(const std::wstring& path) const
{
    std::vector<DayUsage> rows;

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"r, ccs=UTF-8") != 0 || !file) return rows;

    std::wstring buffer(kLineChars, L'\0');
    bool first = true;

    while (fgetws(buffer.data(), static_cast<int>(buffer.size()), file))
    {
        std::wstring line = buffer.c_str();
        while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
        if (first)
        {
            // "ccs=UTF-8" leaves the BOM in the first line.
            if (!line.empty() && line.front() == 0xFEFF) line.erase(line.begin());
            first = false;
        }
        if (line.empty() || line.front() == L'#') continue;

        const std::vector<std::wstring> parts = Fields(line);
        // A truncated last line has fewer fields; skipping it is the whole
        // corruption strategy (判据 3), so nothing here may throw or assert.
        if (parts.size() < 6) continue;

        DayUsage row;
        row.date = parts[0];
        row.name = parts[1];
        row.key = parts[2];
        row.down = ToInt64(parts[3]);
        row.up = ToInt64(parts[4]);
        row.lastSeen = ToInt64(parts[5]);
        if (Ordinal(row.date) < 0) continue;
        rows.push_back(std::move(row));
    }

    fclose(file);
    return rows;
}

void UsageStore::SeedToday(const std::wstring& today, UsageLedger& ledger) const
{
    for (const DayUsage& row : rows_)
    {
        if (row.date != today) continue;
        ledger.Seed(row.key, row.name, row.down, row.up);
    }
}

bool UsageStore::MaybeSave(const std::wstring& today, const UsageLedger& ledger, double nowSeconds,
                           bool force)
{
    const int64_t total = ledger.TotalDown() + ledger.TotalUp();

    if (!force)
    {
        // Nothing new since the last save (idle, or a paused sampler): rewriting
        // the file every 30 s would churn the disk for no reason.
        if (lastSavedAt_ >= 0 && total == lastSavedTotal_) return false;

        if (lastSavedAt_ >= 0 && nowSeconds - lastSavedAt_ < SaveIntervalSeconds &&
            total - lastSavedTotal_ < SaveDeltaBytes)
        {
            return false;
        }
    }

    const bool ok = WriteMonth(today, ledger);
    lastSavedAt_ = nowSeconds;
    lastSavedTotal_ = total;
    return ok;
}

bool UsageStore::WriteMonth(const std::wstring& today, const UsageLedger& ledger)
{
    if (today.size() < 7)
    {
        error_ = L"bad date: " + today;
        return false;
    }

    const std::wstring month = today.substr(0, 7);
    const std::wstring path = dir_ + L"\\" + month + L".tsv";
    const std::wstring temp = path + L".tmp";

    std::vector<DayUsage> out;
    for (const DayUsage& row : rows_)
    {
        if (row.date == today) continue;                      // rewritten from the ledger
        if (row.date.substr(0, 7) != month) continue;         // other months stay untouched
        out.push_back(row);
    }

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    for (const UsageAccount& account : ledger.Accounts())
    {
        DayUsage row;
        row.date = today;
        row.key = account.key;
        row.name = account.name;
        row.down = account.down;
        row.up = account.up;
        row.lastSeen = now;
        out.push_back(std::move(row));
    }

    std::sort(out.begin(), out.end(), [](const DayUsage& left, const DayUsage& right) {
        if (left.date != right.date) return left.date < right.date;
        return left.name < right.name;
    });

    FILE* file = nullptr;
    const errno_t opened = _wfopen_s(&file, temp.c_str(), L"w, ccs=UTF-8");
    if (opened != 0 || !file)
    {
        error_ = L"open failed: " + std::to_wstring(opened);
        return false;
    }

    fwprintf(file, L"# date\texe\tpath\tdown\tup\tlastSeen\n");
    for (const DayUsage& row : out)
    {
        fwprintf(file, L"%ls\t%ls\t%ls\t%lld\t%lld\t%lld\n", row.date.c_str(),
                 Sanitize(row.name).c_str(), Sanitize(row.key).c_str(),
                 static_cast<long long>(row.down), static_cast<long long>(row.up),
                 static_cast<long long>(row.lastSeen));
    }

    const bool flushed = fflush(file) == 0;
    fclose(file);

    if (!flushed)
    {
        DeleteFileW(temp.c_str());
        error_ = L"flush failed";
        return false;
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        const DWORD last = GetLastError();
        DeleteFileW(temp.c_str());
        error_ = L"rename failed: " + std::to_wstring(last);
        return false;
    }

    // Keep the in-memory copy in step so Aggregate() never reads stale rows.
    // `out` already holds every row of this month (minus today's, which came from
    // the ledger), so the month must be replaced wholesale - appending would
    // duplicate yesterday and double it in the next aggregate.
    std::vector<DayUsage> kept;
    kept.reserve(rows_.size());
    for (const DayUsage& row : rows_)
    {
        if (row.date.substr(0, 7) != month) kept.push_back(row);
    }
    rows_ = std::move(kept);
    rows_.insert(rows_.end(), out.begin(), out.end());

    error_.clear();
    return true;
}

std::vector<UsageRow> UsageStore::Aggregate(int days, const std::wstring& today,
                                           const UsageLedger& ledger) const
{
    if (days < 1) days = 1;

    const int64_t cutoff = Ordinal(today) - (days - 1);
    std::map<std::wstring, UsageRow> byKey;

    for (const DayUsage& row : rows_)
    {
        if (row.date == today) continue; // the ledger already carries today in full
        if (Ordinal(row.date) < cutoff) continue;

        UsageRow& out = byKey[row.key];
        out.name = row.name;
        out.down += row.down;
        out.up += row.up;
    }

    for (const UsageAccount& account : ledger.Accounts())
    {
        UsageRow& out = byKey[account.key];
        out.name = account.name;
        out.down += account.down;
        out.up += account.up;
    }

    std::vector<UsageRow> rows;
    rows.reserve(byKey.size());
    for (auto& [key, row] : byKey) rows.push_back(std::move(row));

    std::sort(rows.begin(), rows.end(), [](const UsageRow& left, const UsageRow& right) {
        const int64_t leftTotal = left.down + left.up;
        const int64_t rightTotal = right.down + right.up;
        if (leftTotal != rightTotal) return leftTotal > rightTotal;
        return left.down > right.down;
    });

    return rows;
}
