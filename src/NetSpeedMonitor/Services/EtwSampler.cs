using System.Net;
using Microsoft.Diagnostics.Tracing.Parsers;
using Microsoft.Diagnostics.Tracing.Session;

namespace NetSpeedMonitor.Services;

public sealed class EtwCounters
{
    public long LoopbackIn;
    public long LoopbackOut;
    public long ExternalIn;
    public long ExternalOut;

    public EtwCounters Clone() => (EtwCounters)MemberwiseClone();
}

/// <summary>
/// Kernel-network ETW consumer. Needs elevation; when not elevated IsRunning
/// stays false and the UI falls back to the restricted mode.
///
/// Every transfer is logged once per endpoint, so a local proxy produces two
/// copies of the same bytes (app -> proxy on loopback, proxy -> internet).
/// Counters are therefore split into loopback and external segments and the
/// attribution rules live in <see cref="ProcessRates"/>.
/// </summary>
public sealed class EtwSampler : IDisposable
{
    private readonly object _gate = new();
    private readonly Dictionary<int, EtwCounters> _counters = new();
    private readonly TraceEventSession? _session;
    private readonly Thread? _pump;
    private bool _disposed;

    public bool IsRunning => _session is not null;
    public string? Error { get; }

    public EtwSampler()
    {
        if (!Elevation.IsElevated()) return;

        try
        {
            _session = new TraceEventSession(
                $"NetSpeedMonitor-{Environment.ProcessId}",
                TraceEventSessionOptions.Create);

            _session.EnableKernelProvider(KernelTraceEventParser.Keywords.NetworkTCPIP);

            _session.Source.Kernel.TcpIpSend += d => Add(d.ProcessID, d.saddr, d.daddr, d.size, true);
            _session.Source.Kernel.TcpIpRecv += d => Add(d.ProcessID, d.saddr, d.daddr, d.size, false);
            _session.Source.Kernel.UdpIpSend += d => Add(d.ProcessID, d.saddr, d.daddr, d.size, true);
            _session.Source.Kernel.UdpIpRecv += d => Add(d.ProcessID, d.saddr, d.daddr, d.size, false);

            _pump = new Thread(() =>
            {
                try
                {
                    _session.Source.Process();
                }
                catch
                {
                    // Session torn down while pumping; nothing to report.
                }
            })
            {
                IsBackground = true,
                Name = "etw-pump",
            };
            _pump.Start();
        }
        catch (Exception ex)
        {
            Error = $"{ex.GetType().Name}: {ex.Message}";
            _session?.Dispose();
            _session = null;
        }
    }

    public Dictionary<int, EtwCounters> Snapshot()
    {
        lock (_gate)
        {
            var copy = new Dictionary<int, EtwCounters>(_counters.Count);
            foreach ((int pid, EtwCounters counters) in _counters)
            {
                copy[pid] = counters.Clone();
            }

            return copy;
        }
    }

    private void Add(int pid, IPAddress saddr, IPAddress daddr, int size, bool sent)
    {
        if (size <= 0) return;

        bool loopback = (saddr is not null && IPAddress.IsLoopback(saddr))
                        || (daddr is not null && IPAddress.IsLoopback(daddr));

        lock (_gate)
        {
            if (!_counters.TryGetValue(pid, out EtwCounters? counters))
            {
                counters = new EtwCounters();
                _counters[pid] = counters;
            }

            if (loopback)
            {
                if (sent) counters.LoopbackOut += size;
                else counters.LoopbackIn += size;
            }
            else
            {
                if (sent) counters.ExternalOut += size;
                else counters.ExternalIn += size;
            }
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        try
        {
            _session?.Stop();
        }
        catch
        {
            // Already stopped.
        }

        _session?.Dispose();
    }
}
