using System.Diagnostics;
using NetSpeedMonitor.Interop;

namespace NetSpeedMonitor.Services;

/// <summary>
/// Samples one interface through GetIfEntry. The counters are 32-bit and wrap,
/// so deltas use unchecked subtraction; the sample is discarded when the gap is
/// longer than <see cref="ResetThresholdSeconds"/> (sleep, suspend, debugger).
/// </summary>
public sealed class AdapterSampler
{
    private const double ResetThresholdSeconds = 5;

    private uint _index;
    private uint _lastIn;
    private uint _lastOut;
    private long _lastTimestamp;
    private bool _hasBaseline;

    public string AdapterName { get; private set; } = "未连接";

    public uint Index => _index;

    /// <summary>Pins the sampler to one interface; 0 means "no interface".</summary>
    public bool Select(uint index)
    {
        if (index == _index) return true;

        _index = index;
        _hasBaseline = false;
        AdapterName = index == 0
            ? "未连接"
            : AdapterCatalog.NameOf(index) ?? $"接口 {index}";
        return true;
    }

    /// <summary>
    /// Re-reads the default route and switches when it moved. Called every few
    /// seconds so a cable/Wi-Fi change is picked up without a restart.
    /// </summary>
    public bool FollowDefaultRoute() => Select(DefaultRoute.GetDefaultInterfaceIndex());

    /// <summary>Kept for the probe, which always measures the default route.</summary>
    public bool SelectDefaultAdapter() => FollowDefaultRoute();

    /// <summary>Drops the previous counters, e.g. after the machine wakes up.</summary>
    public void ResetBaseline() => _hasBaseline = false;

    public bool TryReadRaw(out uint inOctets, out uint outOctets)
    {
        if (_index == 0)
        {
            inOctets = 0;
            outOctets = 0;
            return false;
        }

        return TryRead(_index, out inOctets, out outOctets);
    }

    public (double Down, double Up) Sample()
    {
        if (_index == 0) return (0, 0);
        if (!TryRead(_index, out uint inNow, out uint outNow)) return (0, 0);

        long now = Stopwatch.GetTimestamp();

        if (!_hasBaseline)
        {
            Remember(inNow, outNow, now);
            return (0, 0);
        }

        double seconds = (now - _lastTimestamp) / (double)Stopwatch.Frequency;
        if (seconds <= 0) return (0, 0);

        if (seconds > ResetThresholdSeconds)
        {
            Remember(inNow, outNow, now);
            return (0, 0);
        }

        uint deltaIn = unchecked(inNow - _lastIn);
        uint deltaOut = unchecked(outNow - _lastOut);
        Remember(inNow, outNow, now);

        return (deltaIn / seconds, deltaOut / seconds);
    }

    private void Remember(uint inOctets, uint outOctets, long timestamp)
    {
        _lastIn = inOctets;
        _lastOut = outOctets;
        _lastTimestamp = timestamp;
        _hasBaseline = true;
    }

    private static bool TryRead(uint index, out uint inOctets, out uint outOctets)
    {
        inOctets = 0;
        outOctets = 0;

        NativeMethods.MIB_IFROW row = NativeMethods.CreateIfRow(index);
        if (NativeMethods.GetIfEntry(ref row) != NativeMethods.ErrorSuccess) return false;

        inOctets = row.dwInOctets;
        outOctets = row.dwOutOctets;
        return true;
    }
}
