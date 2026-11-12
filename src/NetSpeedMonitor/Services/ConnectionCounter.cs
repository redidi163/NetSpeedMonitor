using System.Runtime.InteropServices;
using NetSpeedMonitor.Interop;

namespace NetSpeedMonitor.Services;

public sealed record ProcessConnectionRow(
    string Name,
    int Count,
    string CountText,
    double BarWidth);

/// <summary>
/// TCP connection counts per PID, available without elevation. The restricted
/// mode shows these instead of byte rates, which need ETW.
/// </summary>
public static class ConnectionCounter
{
    public const double BarMaxWidth = 120;

    public static Dictionary<int, int> CountByPid()
    {
        var counts = new Dictionary<int, int>();

        CountV4(counts);
        CountV6(counts);

        return counts;
    }

    public static List<ProcessConnectionRow> BuildRows(
        IReadOnlyDictionary<int, int> counts,
        Func<int, string> nameOf,
        int limit = 5)
    {
        List<(string Name, int Count)> merged = counts
            .Where(pair => pair.Value > 0)
            .GroupBy(pair => nameOf(pair.Key))
            .Select(group => (Name: group.Key, Count: group.Sum(pair => pair.Value)))
            .OrderByDescending(row => row.Count)
            .ThenBy(row => row.Name, StringComparer.OrdinalIgnoreCase)
            .Take(limit)
            .ToList();

        int max = merged.Count == 0 ? 1 : merged.Max(row => row.Count);

        return merged
            .Select(row => new ProcessConnectionRow(
                row.Name,
                row.Count,
                $"{row.Count} 连接",
                BarMaxWidth * row.Count / max))
            .ToList();
    }

    private static void CountV4(Dictionary<int, int> counts)
    {
        int size = 0;
        uint result = NativeMethods.GetExtendedTcpTable(
            IntPtr.Zero, ref size, false, NativeMethods.AfInet, NativeMethods.TcpTableOwnerPidAll, 0);

        if (result != NativeMethods.ErrorInsufficientBuffer || size <= 0) return;

        IntPtr buffer = Marshal.AllocHGlobal(size);
        try
        {
            result = NativeMethods.GetExtendedTcpTable(
                buffer, ref size, false, NativeMethods.AfInet, NativeMethods.TcpTableOwnerPidAll, 0);
            if (result != 0) return;

            int rows = Marshal.ReadInt32(buffer);
            int rowSize = Marshal.SizeOf<NativeMethods.MIB_TCPROW_OWNER_PID>();
            IntPtr row = IntPtr.Add(buffer, sizeof(int));

            for (int i = 0; i < rows; i++, row = IntPtr.Add(row, rowSize))
            {
                var entry = Marshal.PtrToStructure<NativeMethods.MIB_TCPROW_OWNER_PID>(row);
                if (entry.dwState != NativeMethods.TcpStateEstablished) continue;
                Bump(counts, (int)entry.dwOwningPid);
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private static void CountV6(Dictionary<int, int> counts)
    {
        int size = 0;
        uint result = NativeMethods.GetExtendedTcpTable(
            IntPtr.Zero, ref size, false, NativeMethods.AfInet6, NativeMethods.TcpTableOwnerPidAll, 0);

        if (result != NativeMethods.ErrorInsufficientBuffer || size <= 0) return;

        IntPtr buffer = Marshal.AllocHGlobal(size);
        try
        {
            result = NativeMethods.GetExtendedTcpTable(
                buffer, ref size, false, NativeMethods.AfInet6, NativeMethods.TcpTableOwnerPidAll, 0);
            if (result != 0) return;

            int rows = Marshal.ReadInt32(buffer);
            int rowSize = Marshal.SizeOf<NativeMethods.MIB_TCP6ROW_OWNER_PID>();
            IntPtr row = IntPtr.Add(buffer, sizeof(int));

            for (int i = 0; i < rows; i++, row = IntPtr.Add(row, rowSize))
            {
                var entry = Marshal.PtrToStructure<NativeMethods.MIB_TCP6ROW_OWNER_PID>(row);
                if (entry.dwState != NativeMethods.TcpStateEstablished) continue;
                Bump(counts, (int)entry.dwOwningPid);
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private static void Bump(Dictionary<int, int> counts, int pid)
    {
        if (pid <= 0) return;
        counts[pid] = counts.TryGetValue(pid, out int current) ? current + 1 : 1;
    }
}
