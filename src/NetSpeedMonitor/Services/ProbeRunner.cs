using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;

namespace NetSpeedMonitor.Services;

/// <summary>
/// Headless sampling mode used to verify readings against an independent source.
/// Writes one CSV row per second: elapsed_ms,down_bps,up_bps,interval_seconds.
/// </summary>
public static class ProbeRunner
{
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
}
