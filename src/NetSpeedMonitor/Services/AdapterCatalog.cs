using System.Net.NetworkInformation;

namespace NetSpeedMonitor.Services;

public sealed record AdapterOption(uint Index, string Name, bool Up)
{
    public string Label => Up ? Name : $"{Name}（未连接）";
}

/// <summary>
/// Interfaces the user can pin as the measurement source. Loopback and tunnel
/// adapters are excluded: they carry traffic that is already counted elsewhere.
/// </summary>
public static class AdapterCatalog
{
    public static List<AdapterOption> List()
    {
        var options = new List<AdapterOption>();

        foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (ni.NetworkInterfaceType is NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel) continue;

            int? index = TryGetIndex(ni);
            if (index is null or 0) continue;

            options.Add(new AdapterOption(
                (uint)index.Value,
                ni.Name,
                ni.OperationalStatus == OperationalStatus.Up));
        }

        return options
            .OrderByDescending(option => option.Up)
            .ThenBy(option => option.Name, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    public static string? NameOf(uint index)
    {
        foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (TryGetIndex(ni) == (int)index) return ni.Name;
        }

        return null;
    }

    private static int? TryGetIndex(NetworkInterface ni)
    {
        IPInterfaceProperties props;
        try
        {
            props = ni.GetIPProperties();
        }
        catch (NetworkInformationException)
        {
            return null;
        }

        // GetIPv4/IPv6Properties throw 10043 when the protocol is not bound.
        try
        {
            IPv4InterfaceProperties? v4 = props.GetIPv4Properties();
            if (v4 is not null) return v4.Index;
        }
        catch (NetworkInformationException) { }

        try
        {
            IPv6InterfaceProperties? v6 = props.GetIPv6Properties();
            if (v6 is not null) return v6.Index;
        }
        catch (NetworkInformationException) { }

        return null;
    }
}
