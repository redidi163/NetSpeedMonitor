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
        Dictionary<int, EtwCounters> baseline = etw.Snapshot();
        long firstEtwIn = ExternalIn(baseline);
        long firstEtwOut = ExternalOut(baseline);

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

        string loadState;
        long payloadBytes = -1;

        if (loadProcess is null)
        {
            loadState = "not_started";
        }
        else
        {
            bool exited = loadProcess.WaitForExit(25_000);
            if (!exited)
            {
                try
                {
                    loadProcess.Kill(true);
                }
                catch
                {
                    // Already gone.
                }
            }

            loadState = exited ? $"exited={loadProcess.ExitCode}" : "killed";

            try
            {
                string text = loadProcess.StandardOutput.ReadToEnd().Trim();
                if (long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out long parsed))
                {
                    payloadBytes = parsed;
                }
            }
            catch
            {
                // No output to read.
            }
        }

        // Let the ETW pump drain the events for the last bytes curl received.
        Thread.Sleep(1200);

        // The final snapshot is taken after the load process exits, so the
        // cumulative window covers the whole download even when it runs past
        // the sampling loop.
        adapter.TryReadRaw(out uint lastIn, out uint lastOut);
        Dictionary<int, EtwCounters> final = etw.Snapshot();
        double totalSeconds = (Stopwatch.GetTimestamp() - startTicks) / (double)Stopwatch.Frequency;

        long adapterIn = unchecked((uint)(lastIn - firstIn));
        long adapterOut = unchecked((uint)(lastOut - firstOut));
        long adapterTotal = adapterIn + adapterOut;
        long etwIn = ExternalIn(final) - firstEtwIn;
        long etwOut = ExternalOut(final) - firstEtwOut;
        long etwTotal = etwIn + etwOut;
        double diffPct = adapterTotal == 0 ? 0 : (etwTotal - adapterTotal) * 100.0 / adapterTotal;
        double diffInPct = adapterIn == 0 ? 0 : (etwIn - adapterIn) * 100.0 / adapterIn;
        double diffOutPct = adapterOut == 0 ? 0 : (etwOut - adapterOut) * 100.0 / adapterOut;

        long loadEtw = 0;
        if (loadProcess is not null && final.TryGetValue(loadProcess.Id, out EtwCounters? loadCounters))
        {
            loadEtw = loadCounters.ExternalIn + loadCounters.ExternalOut;
        }

        double vsPayload = payloadBytes <= 0
            ? 0
            : (loadEtw - payloadBytes) * 100.0 / payloadBytes;

        builder.AppendLine(string.Create(CultureInfo.InvariantCulture,
            $"# summary seconds={totalSeconds:0.00} adapter_in={adapterIn} adapter_out={adapterOut} adapter_total={adapterTotal} etw_in={etwIn} etw_out={etwOut} etw_external={etwTotal} diff_pct={diffPct:0.00} diff_in_pct={diffInPct:0.00} diff_out_pct={diffOutPct:0.00} etw_lost={etw.EventsLost} load_pid={loadProcess?.Id ?? 0} load_etw={loadEtw} payload={payloadBytes} vs_payload_pct={vsPayload:0.00} load={loadState}"));

        File.WriteAllText(outputPath, builder.ToString(), Encoding.UTF8);
    }

    /// <summary>
    /// Downloads a fixed 12 MB range so the exact payload size is known: curl
    /// prints it to stdout via -w, which gives an independent byte count to
    /// compare the ETW counters against.
    /// </summary>
    private static Process? TryStartLoad()
    {
        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = "curl.exe",
                Arguments = "-s --fail --max-time 60 -r 0-11999999 -o NUL -w \"%{size_download}\" \"https://mirrors.tuna.tsinghua.edu.cn/ubuntu-releases/22.04/ubuntu-22.04.5-desktop-amd64.iso\"",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
            };

            return Process.Start(startInfo);
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

    private static long ExternalIn(IReadOnlyDictionary<int, EtwCounters> snapshot)
    {
        long total = 0;
        foreach (EtwCounters counters in snapshot.Values)
        {
            total += counters.ExternalIn;
        }

        return total;
    }

    private static long ExternalOut(IReadOnlyDictionary<int, EtwCounters> snapshot)
    {
        long total = 0;
        foreach (EtwCounters counters in snapshot.Values)
        {
            total += counters.ExternalOut;
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
