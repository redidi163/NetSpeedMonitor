namespace NetSpeedMonitor.Services;

/// <summary>
/// Identity of a running process. <paramref name="Key"/> is the merge key
/// (exe path), <paramref name="StartTicks"/> tells a reused PID apart from the
/// process that held it before.
/// </summary>
public readonly record struct ProcessIdentity(string Key, string Name, long StartTicks);

/// <summary>
/// Turns raw per-PID ETW counters into the Top 5 rows.
///
/// Three rules live here:
/// - rows are merged by exe path, so three instances of the same exe take one row;
/// - sorting uses a 3 tick sliding average while the numbers stay instantaneous,
///   which keeps the order from flipping on a single second of jitter;
/// - a process keeps its row for up to 5 seconds after its last byte, decaying
///   through the average instead of vanishing.
/// </summary>
public sealed class ProcessAggregator
{
    public const int WindowTicks = 3;
    public const double RemoveAfterSeconds = 5;

    /// <summary>
    /// Rows below this average are noise: without a floor, a process that moved
    /// a few kilobytes of keepalive sits in the Top 5 next to real traffic.
    /// </summary>
    public const double MinVisibleBytesPerSecond = 1024;

    /// <summary>A gap longer than this means the machine slept; the counters are stale.</summary>
    public const double MaxGapSeconds = 5;

    private sealed class Entry
    {
        public string Name = "";
        public readonly Queue<double> Down = new(WindowTicks + 1);
        public readonly Queue<double> Up = new(WindowTicks + 1);
        public double LastDown;
        public double LastUp;
        public double LastSeen;
    }

    private sealed class TickSum
    {
        public string Name = "";
        public double Down;
        public double Up;
    }

    private readonly Func<int, ProcessIdentity> _identityOf;
    private readonly Dictionary<string, Entry> _entries = new();
    private readonly Dictionary<int, long> _pidStart = new();
    private IReadOnlyDictionary<int, EtwCounters> _previous = new Dictionary<int, EtwCounters>();
    private double _lastTick = double.NaN;

    public ProcessAggregator(Func<int, ProcessIdentity> identityOf) => _identityOf = identityOf;

    /// <param name="nowSeconds">Monotonic seconds; passed in so tests control the clock.</param>
    public (List<ProcessRateRow> Top, double ExternalBytesPerSecond) Update(
        IReadOnlyDictionary<int, EtwCounters> snapshot,
        double nowSeconds)
    {
        if (double.IsNaN(_lastTick))
        {
            Prime(snapshot, nowSeconds);
            return ([], 0);
        }

        double seconds = nowSeconds - _lastTick;
        if (seconds > MaxGapSeconds)
        {
            Reset();
            Prime(snapshot, nowSeconds);
            return ([], 0);
        }

        _lastTick = nowSeconds;
        if (seconds <= 0) seconds = 1;

        double externalTotal = 0;
        var tickSums = new Dictionary<string, TickSum>();

        foreach ((int pid, EtwCounters now) in snapshot)
        {
            ProcessIdentity identity = _identityOf(pid);

            if (_pidStart.TryGetValue(pid, out long knownStart) && knownStart != identity.StartTicks)
            {
                // The PID now belongs to a different process: drop this tick so
                // the new process does not inherit the old one's bytes.
                _pidStart[pid] = identity.StartTicks;
                continue;
            }

            _pidStart[pid] = identity.StartTicks;

            _previous.TryGetValue(pid, out EtwCounters? before);
            before ??= new EtwCounters();

            PidDelta delta = ProcessRates.Delta(before, now);
            externalTotal += delta.External;

            if (ProcessRates.IsLocalProxy(delta)) continue;
            if (delta.In == 0 && delta.Out == 0) continue;

            if (!tickSums.TryGetValue(identity.Key, out TickSum? sum))
            {
                sum = new TickSum { Name = identity.Name };
                tickSums[identity.Key] = sum;
            }

            sum.Down += delta.In / seconds;
            sum.Up += delta.Out / seconds;
        }

        foreach ((string key, TickSum sum) in tickSums)
        {
            if (!_entries.TryGetValue(key, out Entry? entry))
            {
                entry = new Entry();
                _entries[key] = entry;
            }

            entry.Name = sum.Name;
            entry.LastDown = sum.Down;
            entry.LastUp = sum.Up;
            entry.LastSeen = nowSeconds;
            Push(entry.Down, sum.Down);
            Push(entry.Up, sum.Up);
        }

        foreach ((string key, Entry entry) in _entries)
        {
            if (tickSums.ContainsKey(key)) continue;

            // No bytes this tick: decay through the window instead of snapping to zero.
            Push(entry.Down, 0);
            Push(entry.Up, 0);
            entry.LastDown = 0;
            entry.LastUp = 0;
        }

        foreach (string key in _entries
                     .Where(e => nowSeconds - e.Value.LastSeen > RemoveAfterSeconds)
                     .Select(e => e.Key)
                     .ToList())
        {
            _entries.Remove(key);
        }

        _previous = snapshot;
        return (BuildRows(), externalTotal / seconds);
    }

    public void Reset()
    {
        _entries.Clear();
        _pidStart.Clear();
        _previous = new Dictionary<int, EtwCounters>();
        _lastTick = double.NaN;
    }

    private void Prime(IReadOnlyDictionary<int, EtwCounters> snapshot, double nowSeconds)
    {
        _lastTick = nowSeconds;
        _previous = snapshot;

        foreach ((int pid, _) in snapshot)
        {
            _pidStart[pid] = _identityOf(pid).StartTicks;
        }
    }

    private static void Push(Queue<double> queue, double value)
    {
        queue.Enqueue(value);
        while (queue.Count > WindowTicks) queue.Dequeue();
    }

    private static double Average(Queue<double> queue) => queue.Count == 0 ? 0 : queue.Average();

    private List<ProcessRateRow> BuildRows()
    {
        var rows = new List<(string Name, double Score, double Down, double Up)>(_entries.Count);

        foreach (Entry entry in _entries.Values)
        {
            double score = Average(entry.Down) + Average(entry.Up);

            // Nothing in the whole window means the process stopped talking;
            // hide it now instead of leaving a 0/0 row on screen.
            if (score < MinVisibleBytesPerSecond) continue;

            rows.Add((entry.Name, score, entry.LastDown, entry.LastUp));
        }

        double max = Math.Max(1, rows.Count == 0 ? 1 : rows.Max(r => r.Score));

        return rows
            .OrderByDescending(r => r.Score)
            .ThenByDescending(r => r.Down + r.Up)
            .Take(5)
            .Select(r => new ProcessRateRow(
                r.Name,
                r.Down,
                r.Up,
                ProcessRates.FormatRate(r.Down),
                ProcessRates.FormatRate(r.Up),
                ProcessRates.BarMaxWidth * r.Score / max))
            .ToList();
    }
}
