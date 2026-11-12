using System.Diagnostics;
using System.Globalization;
using System.Security.Principal;
using System.Text;
using Microsoft.Diagnostics.Tracing.Parsers;
using Microsoft.Diagnostics.Tracing.Session;

string output = args.Length > 0 ? args[0] : Path.Combine(Path.GetTempPath(), "etw-spike.txt");
int seconds = args.Length > 1 ? int.Parse(args[1], CultureInfo.InvariantCulture) : 10;

var log = new StringBuilder();
log.AppendLine($"elevated={IsElevated()} started={DateTime.Now:O} seconds={seconds}");

var counts = new Dictionary<int, long[]>();
object gate = new();

void Add(int pid, int bytes, int slot)
{
    lock (gate)
    {
        if (!counts.TryGetValue(pid, out long[]? arr))
        {
            arr = new long[4];
            counts[pid] = arr;
        }

        arr[slot] += bytes;
    }
}

try
{
    using var session = new TraceEventSession("NetSpeedMonitorSpike", TraceEventSessionOptions.Create);
    session.EnableKernelProvider(KernelTraceEventParser.Keywords.NetworkTCPIP);
    log.AppendLine("mode=kernel-provider NetworkTCPIP");

    session.Source.Kernel.TcpIpSend += d => Add(d.ProcessID, d.size, 0);
    session.Source.Kernel.TcpIpRecv += d => Add(d.ProcessID, d.size, 1);
    session.Source.Kernel.UdpIpSend += d => Add(d.ProcessID, d.size, 2);
    session.Source.Kernel.UdpIpRecv += d => Add(d.ProcessID, d.size, 3);

    var stopper = new Thread(() =>
    {
        Thread.Sleep(seconds * 1000);
        session.Stop();
    })
    { IsBackground = true };
    stopper.Start();

    session.Source.Process();
    log.AppendLine($"collected_pids={counts.Count}");
}
catch (Exception ex)
{
    log.AppendLine($"error={ex.GetType().Name}: {ex.Message}");
}

foreach ((int pid, long[] arr) in counts.OrderByDescending(kv => kv.Value[0] + kv.Value[1]).Take(12))
{
    string name = "";
    try
    {
        name = Process.GetProcessById(pid).ProcessName;
    }
    catch
    {
        // process already exited
    }

    log.AppendLine($"{pid}\t{name}\ttcp_sent={arr[0]}\ttcp_recv={arr[1]}\tudp_sent={arr[2]}\tudp_recv={arr[3]}");
}

File.WriteAllText(output, log.ToString(), Encoding.UTF8);
Console.WriteLine(log.ToString());
Console.WriteLine($"written={output}");

static bool IsElevated() =>
    new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
