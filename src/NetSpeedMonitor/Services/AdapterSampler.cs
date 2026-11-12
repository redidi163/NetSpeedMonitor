using System.Diagnostics;
using System.Net.NetworkInformation;
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

    public bool SelectDefaultAdapter()
    {
        uint index = DefaultRoute.GetDefaultInterfaceIndex();
        if (index == 0) return false;
        if (index == _index) return true;

        _index = index;
        _hasBaseline = false;
        AdapterName = ResolveName(index) ?? $"接口 {index}";
        return true;
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

    private static string? ResolveName(uint index)
    {
        foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            IPInterfaceProperties props;
            try
            {
                props = ni.GetIPProperties();
            }
            catch (NetworkInformationException)
            {
                continue;
            }

            // GetIPv4/IPv6Properties throw 10043 on interfaces where the
            // protocol is not bound; treat that as "no match", not a failure.
            try
            {
                if (props.GetIPv4Properties()?.Index == (int)index) return ni.Name;
            }
            catch (NetworkInformationException) { }

            try
            {
                if (props.GetIPv6Properties()?.Index == (int)index) return ni.Name;
            }
            catch (NetworkInformationException) { }
        }

        return null;
    }
}
