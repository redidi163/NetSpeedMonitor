using NetSpeedMonitor.Services;

namespace NetSpeedMonitor.Tests;

public class ConnectionCounterTests
{
    [Fact]
    public void MergesInstancesAndSortsByConnectionCount()
    {
        var counts = new Dictionary<int, int>
        {
            [10] = 2,
            [11] = 3,
            [20] = 4,
            [30] = 1,
        };

        var names = new Dictionary<int, string>
        {
            [10] = "chrome.exe",
            [11] = "chrome.exe",
            [20] = "svchost.exe",
            [30] = "explorer.exe",
        };

        List<ProcessConnectionRow> rows = ConnectionCounter.BuildRows(counts, pid => names[pid]);

        Assert.Equal(3, rows.Count);
        Assert.Equal("chrome.exe", rows[0].Name);
        Assert.Equal(5, rows[0].Count);
        Assert.Equal("5 连接", rows[0].CountText);
        Assert.Equal("svchost.exe", rows[1].Name);
        Assert.Equal("explorer.exe", rows[2].Name);
    }

    [Fact]
    public void RespectsTheRowLimitAndSkipsZeroCounts()
    {
        var counts = new Dictionary<int, int>
        {
            [1] = 9,
            [2] = 8,
            [3] = 7,
            [4] = 6,
            [5] = 5,
            [6] = 4,
            [7] = 0,
        };

        List<ProcessConnectionRow> rows = ConnectionCounter.BuildRows(counts, pid => $"p{pid}.exe", limit: 5);

        Assert.Equal(5, rows.Count);
        Assert.Equal("p1.exe", rows[0].Name);
        Assert.Equal("p5.exe", rows[4].Name);
    }
}
