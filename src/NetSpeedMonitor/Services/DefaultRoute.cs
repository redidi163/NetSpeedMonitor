using System.Net.NetworkInformation;
using NetSpeedMonitor.Interop;

namespace NetSpeedMonitor.Services;

public static class DefaultRoute
{
    /// <summary>
    /// GetBestInterface(0) asks Windows to route to 0.0.0.0, which resolves to
    /// the default route. Returns 0 when there is no usable route.
    /// </summary>
    public static uint GetDefaultInterfaceIndex()
    {
        if (NativeMethods.GetBestInterface(0, out uint index) == NativeMethods.ErrorSuccess && index != 0)
        {
            return index;
        }

        return FallbackByGateway();
    }

    private static uint FallbackByGateway()
    {
        foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (ni.OperationalStatus != OperationalStatus.Up) continue;
            if (ni.NetworkInterfaceType is NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel) continue;

            IPInterfaceProperties props;
            try
            {
                props = ni.GetIPProperties();
            }
            catch (NetworkInformationException)
            {
                continue;
            }

            if (props.GatewayAddresses.Count == 0) continue;

            try
            {
                IPv4InterfaceProperties? v4 = props.GetIPv4Properties();
                if (v4 is not null) return (uint)v4.Index;
            }
            catch (NetworkInformationException) { }
        }

        return 0;
    }
}
