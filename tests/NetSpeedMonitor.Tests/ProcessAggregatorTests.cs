using NetSpeedMonitor.Services;

namespace NetSpeedMonitor.Tests;

public class ProcessAggregatorTests
{
    private const double Tick = 1.0;

    /// <summary>Builds a resolver that returns a fixed identity per PID.</summary>
    private static ProcessAggregator Aggregator(params (int Pid, string Key, string Name, long Start)[] processes)
    {
        Dictionary<int, ProcessIdentity> map = processes.ToDictionary(
            p => p.Pid,
            p => new ProcessIdentity(p.Key, p.Name, p.Start));

        return new ProcessAggregator(pid => map.TryGetValue(pid, out ProcessIdentity identity)
            ? identity
            : new ProcessIdentity($"pid:{pid}", $"pid {pid}", 0));
    }

    private static EtwCounters Counters(long externalIn = 0, long externalOut = 0,
        long loopbackIn = 0, long loopbackOut = 0) => new()
    {
        ExternalIn = externalIn,
        ExternalOut = externalOut,
        LoopbackIn = loopbackIn,
        LoopbackOut = loopbackOut,
    };

    private static Dictionary<int, EtwCounters> Snapshot(params (int Pid, EtwCounters Counters)[] rows) =>
        rows.ToDictionary(r => r.Pid, r => r.Counters);

    [Fact]
    public void MergesPidsOfTheSameExeIntoOneRow()
    {
        ProcessAggregator aggregator = Aggregator(
            (100, @"c:\app\chrome.exe", "chrome.exe", 1),
            (200, @"c:\app\chrome.exe", "chrome.exe", 1));

        aggregator.Update(Snapshot((100, Counters()), (200, Counters())), 0);

        (List<ProcessRateRow> top, _) = aggregator.Update(
            Snapshot((100, Counters(externalIn: 1_000_000)), (200, Counters(externalIn: 3_000_000))),
            Tick);

        ProcessRateRow row = Assert.Single(top);
        Assert.Equal("chrome.exe", row.Name);
        Assert.Equal(4_000_000, row.DownBytesPerSecond, 0);
    }

    [Fact]
    public void SortsByThreeSecondAverageNotByInstantaneousValue()
    {
        // steady keeps 500 KB/s, spike fires 1.2 MB once: the average must win.
        ProcessAggregator aggregator = Aggregator(
            (100, @"c:\steady.exe", "steady.exe", 1),
            (200, @"c:\spike.exe", "spike.exe", 1));

        aggregator.Update(Snapshot((100, Counters()), (200, Counters())), 0);

        long steady = 0;
        for (int i = 1; i <= 4; i++)
        {
            steady += 500_000;
            // One byte on the first tick gives the spike process a history, so
            // its later burst is averaged over the window like everyone else's.
            const long spike = 1;
            aggregator.Update(Snapshot((100, Counters(externalIn: steady)), (200, Counters(externalIn: spike))), i * Tick);
        }

        (List<ProcessRateRow> top, _) = aggregator.Update(
            Snapshot((100, Counters(externalIn: steady + 500_000)), (200, Counters(externalIn: 1_200_001))),
            5 * Tick);

        Assert.Equal(2, top.Count);
        Assert.Equal("steady.exe", top[0].Name);
        Assert.Equal(1_200_000, top[1].DownBytesPerSecond, 0);
    }

    [Fact]
    public void RowDisappearsWithinFiveSecondsAfterLastByte()
    {
        ProcessAggregator aggregator = Aggregator((100, @"c:\gone.exe", "gone.exe", 1));

        aggregator.Update(Snapshot((100, Counters())), 0);
        (List<ProcessRateRow> first, _) = aggregator.Update(Snapshot((100, Counters(externalIn: 1_000_000))), 1);
        Assert.Equal(1_000_000, Assert.Single(first).DownBytesPerSecond, 0);

        // The first idle tick shows 0, never a jump upwards.
        (List<ProcessRateRow> decaying, _) = aggregator.Update(Snapshot(), 2);
        Assert.Equal(0, Assert.Single(decaying).DownBytesPerSecond, 0);

        // Three idle ticks empty the window, so the row is gone well inside 5 s.
        aggregator.Update(Snapshot(), 3);
        (List<ProcessRateRow> gone, _) = aggregator.Update(Snapshot(), 4);
        Assert.Empty(gone);
    }

    [Fact]
    public void ReusedPidDoesNotInheritThePreviousProcessBytes()
    {
        var identities = new Dictionary<int, ProcessIdentity>
        {
            [100] = new(@"c:\old.exe", "old.exe", 1),
        };

        var aggregator = new ProcessAggregator(pid => identities.TryGetValue(pid, out ProcessIdentity identity)
            ? identity
            : new ProcessIdentity($"pid:{pid}", $"pid {pid}", 0));

        aggregator.Update(Snapshot((100, Counters())), 0);
        (List<ProcessRateRow> first, _) = aggregator.Update(Snapshot((100, Counters(externalIn: 10_000_000))), 1);
        Assert.Equal("old.exe", Assert.Single(first).Name);

        // Same PID, new start time: the counters now belong to another process.
        identities[100] = new(@"c:\new.exe", "new.exe", 2);

        (_, double reuseTick) = aggregator.Update(Snapshot((100, Counters(externalIn: 10_500_000))), 2);
        Assert.Equal(0, reuseTick, 0);

        (List<ProcessRateRow> next, double after) =
            aggregator.Update(Snapshot((100, Counters(externalIn: 11_000_000))), 3);

        Assert.Equal(500_000, after, 0);
        Assert.Contains(next, row => row.Name == "new.exe" && row.DownBytesPerSecond == 500_000);
    }

    [Fact]
    public void LocalProxyIsHiddenFromRowsButKeptInTheExternalTotal()
    {
        ProcessAggregator aggregator = Aggregator((100, @"c:\mihomo.exe", "mihomo.exe", 1));

        aggregator.Update(Snapshot((100, Counters())), 0);
        (List<ProcessRateRow> top, double external) = aggregator.Update(
            Snapshot((100, Counters(externalIn: 100_000, loopbackIn: 100_000))),
            Tick);

        Assert.Empty(top);
        Assert.Equal(100_000, external, 0);
    }
}
