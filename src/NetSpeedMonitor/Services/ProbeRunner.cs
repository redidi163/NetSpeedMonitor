using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;

namespace NetSpeedMonitor.Services;

/// <summary>
/// Headless sampling modes used to verify readings against an independent source.
/// </summary>
public static class ProbeRunner
{
    /// <summary>
    /// Writes one CSV row per second: elapsed_ms,down_bps,up_bps,interval_s.
    /// </summary>
    public static void Run(int seconds, string outputPath)
    {
        var sampler = new AdapterSampler();
        sampler.SelectDefaultAdapter();

        var builder = new StringBuilder();
        builder.AppendLine("elapsed_ms,down_bps,up_bps,interval_s");

        DateTime startedAt = DateTime.UtcNow;
        long lastSample = 0;

        for (int i = 0; i <= seconds; i++)
        {
            (double down, double up) = sampler.Sample();
            long elapsed = (long)(DateTime.UtcNow - startedAt).TotalMilliseconds;
            double interval = (elapsed - lastSample) / 1000d;
            lastSample = elapsed;

            builder.AppendLine(string.Create(CultureInfo.InvariantCulture,
                $"{elapsed},{down:0},{up:0},{interval:0.000}"));

            if (i < seconds) Thread.Sleep(1000);
        }

        File.WriteAllText(outputPath, builder.ToString(), Encoding.UTF8);
    }

    /// <summary>
    /// Adapter totals plus ETW external-segment totals and the current Top 1.
    /// Requires an elevated process; writes etw_error= when the session fails.
    /// </summary>
    public static void RunEtw(int seconds, string outputPath, bool generateLoad = false)
    {
        var adapter = new AdapterSampler();
        adapter.SelectDefaultAdapter();

        using var etw = new EtwSampler();

        var builder = new StringBuilder();
        if (!etw.IsRunning)
        {
            builder.AppendLine($"etw_error={etw.Error}");
            File.WriteAllText(outputPath, builder.ToString(), Encoding.UTF8);
            return;
        }

        Process? loadProcess = null;

        builder.AppendLine("elapsed_ms,adapter_down,adapter_up,etw_external,top1,top1_down,top1_up,adapter_cum_in,adapter_cum_out,etw_cum_ext");

        Dictionary<int, EtwCounters> previous = etw.Snapshot();
        long previousTicks = Stopwatch.GetTimestamp();
        long startTicks = previousTicks;
        DateTime startedAt = DateTime.UtcNow;

        adapter.TryReadRaw(out uint firstIn, out uint firstOut);
        long firstEtw = ExternalTotal(etw.Snapshot());

        if (generateLoad)
        {
            loadProcess = TryStartLoad();
            Thread.Sleep(1500);
        }

        for (int i = 0; i < seconds; i++)
        {
            Thread.Sleep(1000);

            (double down, double up) = adapter.Sample();
            Dictionary<int, EtwCounters> snapshot = etw.Snapshot();
            long now = Stopwatch.GetTimestamp();
            double interval = (now - previousTicks) / (double)Stopwatch.Frequency;
            if (interval <= 0 || interval > 5) interval = 1;

            (List<ProcessRateRow> top, double external) =
                ProcessRates.Compute(previous, snapshot, interval, ResolveName);

            previous = snapshot;
            previousTicks = now;

            adapter.TryReadRaw(out uint cumIn, out uint cumOut);
            long cumEtw = ExternalTotal(snapshot);
            long elapsed = (long)(DateTime.UtcNow - startedAt).TotalMilliseconds;
            string topName = top.Count > 0 ? top[0].Name : "-";
            double topDown = top.Count > 0 ? top[0].DownBytesPerSecond : 0;
            double topUp = top.Count > 0 ? top[0].UpBytesPerSecond : 0;

            builder.AppendLine(string.Create(CultureInfo.InvariantCulture,
                $"{elapsed},{down:0},{up:0},{external:0},{topName},{topDown:0},{topUp:0},{cumIn},{cumOut},{cumEtw}"));
        }

        adapter.TryReadRaw(out uint lastIn, out uint lastOut);
        long lastEtw = ExternalTotal(etw.Snapshot());
        double totalSeconds = (Stopwatch.GetTimestamp() - startTicks) / (double)Stopwatch.Frequency;

        long adapterIn = unchecked((uint)(lastIn - firstIn));
        long adapterOut = unchecked((uint)(lastOut - firstOut));
        long adapterTotal = adapterIn + adapterOut;
        long etwTotal = lastEtw - firstEtw;
        double diffPct = adapterTotal == 0 ? 0 : (etwTotal - adapterTotal) * 100.0 / adapterTotal;

        string loadState = loadProcess is null
            ? "not_started"
            : loadProcess.HasExited ? $"exited={loadProcess.ExitCode}" : "running";

        try
        {
            if (loadProcess is { HasExited: false }) loadProcess.Kill(true);
        }
        catch
        {
            // Already gone.
        }

        builder.AppendLine(string.Create(CultureInfo.InvariantCulture,
            $"# summary seconds={totalSeconds:0.00} adapter_in={adapterIn} adapter_out={adapterOut} adapter_total={adapterTotal} etw_external={etwTotal} diff_pct={diffPct:0.00} load={loadState}"));

        File.WriteAllText(outputPath, builder.ToString(), Encoding.UTF8);
    }

    private static Process? TryStartLoad()
    {
        try
        {
            return Process.Start(new ProcessStartInfo
            {
                FileName = "curl.exe",
                Arguments = "-s --fail --max-time 60 --limit-rate 2M -o NUL \"https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ls-lR.gz\"",
                UseShellExecute = false,
                CreateNoWindow = true,
            });
        }
        catch
        {
            return null;
        }
    }

    private static long ExternalTotal(IReadOnlyDictionary<int, EtwCounters> snapshot)
    {
        long total = 0;
        foreach (EtwCounters counters in snapshot.Values)
        {
            total += counters.ExternalIn + counters.ExternalOut;
        }

        return total;
    }

    private static string ResolveName(int pid)
    {
        try
        {
            return $"{Process.GetProcessById(pid).ProcessName}.exe";
        }
        catch
        {
            return $"pid {pid}";
        }
    }
}
