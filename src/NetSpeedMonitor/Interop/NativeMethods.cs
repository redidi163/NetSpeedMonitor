using System.Runtime.InteropServices;

namespace NetSpeedMonitor.Interop;

internal static class NativeMethods
{
    internal const int ErrorSuccess = 0;

    [StructLayout(LayoutKind.Sequential)]
    internal struct MIB_IFROW
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 512)]
        public byte[] wszName;

        public uint dwIndex;
        public uint dwType;
        public uint dwMtu;
        public uint dwSpeed;
        public uint dwPhysAddrLen;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public byte[] bPhysAddr;

        public uint dwAdminStatus;
        public uint dwOperStatus;
        public uint dwLastChange;
        public uint dwInOctets;
        public uint dwInUcastPkts;
        public uint dwInNUcastPkts;
        public uint dwInDiscards;
        public uint dwInErrors;
        public uint dwInUnknownProtos;
        public uint dwOutOctets;
        public uint dwOutUcastPkts;
        public uint dwOutNUcastPkts;
        public uint dwOutDiscards;
        public uint dwOutErrors;
        public uint dwOutQLen;
        public uint dwDescrLen;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
        public byte[] bDescr;
    }

    [DllImport("iphlpapi.dll", ExactSpelling = true)]
    internal static extern int GetIfEntry(ref MIB_IFROW pIfRow);

    [DllImport("iphlpapi.dll", ExactSpelling = true)]
    internal static extern int GetBestInterface(uint dwDestAddr, out uint pdwBestIfIndex);

    /// <summary>
    /// GetIfEntry marshals ByValArray fields from the managed instance, so every
    /// array must be allocated before the call or the marshaler throws.
    /// </summary>
    internal static MIB_IFROW CreateIfRow(uint index) => new()
    {
        wszName = new byte[512],
        bPhysAddr = new byte[8],
        bDescr = new byte[256],
        dwIndex = index,
    };
}
