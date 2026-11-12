namespace NetSpeedMonitor.Services;

public sealed record ProcessRateRow(
    string Name,
    double DownBytesPerSecond,
    double UpBytesPerSecond,
    string DownText,
    string UpText,
    double BarWidth);

public static class ProcessRates
{
    /// <summary>
    /// A process that moves more than this in both the loopback and the external
    /// segment in the same window is treated as a local proxy: its counters are
    /// dropped so its downstream clients are not counted twice.
    /// </summary>
    private const long ProxyThresholdBytes = 8192;

    private const double BarMaxWidth = 120;

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

            long loopbackIn = Math.Max(0, now.LoopbackIn - before.LoopbackIn);
            long loopbackOut = Math.Max(0, now.LoopbackOut - before.LoopbackOut);
            long externalIn = Math.Max(0, now.ExternalIn - before.ExternalIn);
            long externalOut = Math.Max(0, now.ExternalOut - before.ExternalOut);

            long loopback = loopbackIn + loopbackOut;
            long external = externalIn + externalOut;
            externalTotal += external;

            if (loopback > ProxyThresholdBytes && external > ProxyThresholdBytes)
            {
                // Local proxy: counted in externalTotal for reconciliation,
                // excluded from the Top list to avoid double counting.
                continue;
            }

            if (loopback == 0 && external == 0) continue;

            rows.Add((nameOf(pid), loopbackIn + externalIn, loopbackOut + externalOut, loopback + external));
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
