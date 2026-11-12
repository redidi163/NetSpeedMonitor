namespace NetSpeedMonitor.Services;

public sealed record ProcessRateRow(
    string Name,
    double DownBytesPerSecond,
    double UpBytesPerSecond,
    string DownText,
    string UpText,
    double BarWidth);

/// <summary>Bytes moved by one PID between two snapshots, split by segment.</summary>
public readonly record struct PidDelta(long In, long Out, long Loopback, long External);

public static class ProcessRates
{
    /// <summary>
    /// A process that moves more than this in both the loopback and the external
    /// segment in the same window is treated as a local proxy: its counters are
    /// dropped so its downstream clients are not counted twice.
    /// </summary>
    public const long ProxyThresholdBytes = 8192;

    public const double BarMaxWidth = 120;

    public static PidDelta Delta(EtwCounters before, EtwCounters now)
    {
        long loopbackIn = Math.Max(0, now.LoopbackIn - before.LoopbackIn);
        long loopbackOut = Math.Max(0, now.LoopbackOut - before.LoopbackOut);
        long externalIn = Math.Max(0, now.ExternalIn - before.ExternalIn);
        long externalOut = Math.Max(0, now.ExternalOut - before.ExternalOut);

        return new PidDelta(
            loopbackIn + externalIn,
            loopbackOut + externalOut,
            loopbackIn + loopbackOut,
            externalIn + externalOut);
    }

    public static bool IsLocalProxy(PidDelta delta) =>
        delta.Loopback > ProxyThresholdBytes && delta.External > ProxyThresholdBytes;

    /// <summary>
    /// Single-window aggregation used by the headless probe. The UI uses
    /// <see cref="ProcessAggregator"/>, which adds the 3 second average and the
    /// exe merge on top of the same primitives.
    /// </summary>
    public static (List<ProcessRateRow> Top, double ExternalBytesPerSecond) Compute(
        IReadOnlyDictionary<int, EtwCounters> previous,
        IReadOnlyDictionary<int, EtwCounters> current,
        double seconds,
        Func<int, string> nameOf)
    {
        if (seconds <= 0) return ([], 0);

        double externalTotal = 0;
        var rows = new List<(string Name, long In, long Out, long Bar)>();

        foreach ((int pid, EtwCounters now) in current)
        {
            previous.TryGetValue(pid, out EtwCounters? before);
            before ??= new EtwCounters();

            PidDelta delta = Delta(before, now);
            externalTotal += delta.External;

            if (IsLocalProxy(delta)) continue;
            if (delta.In == 0 && delta.Out == 0) continue;

            rows.Add((nameOf(pid), delta.In, delta.Out, delta.In + delta.Out));
        }

        if (rows.Count == 0) return ([], externalTotal / seconds);

        long max = Math.Max(1, rows.Max(r => r.Bar));

        List<ProcessRateRow> top = rows
            .OrderByDescending(r => r.Bar)
            .Take(5)
            .Select(r => new ProcessRateRow(
                r.Name,
                r.In / seconds,
                r.Out / seconds,
                FormatRate(r.In / seconds),
                FormatRate(r.Out / seconds),
                BarMaxWidth * r.Bar / max))
            .ToList();

        return (top, externalTotal / seconds);
    }

    public static string FormatRate(double bytesPerSecond)
    {
        if (bytesPerSecond >= 1024 * 1024) return $"{bytesPerSecond / 1024 / 1024:0.0}M";
        if (bytesPerSecond >= 1024) return $"{bytesPerSecond / 1024:0}K";
        return "0";
    }
}
