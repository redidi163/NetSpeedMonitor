using System.Diagnostics;
using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using NetSpeedMonitor.Services;

namespace NetSpeedMonitor;

public partial class MainWindow : Window
{
    private readonly AdapterSampler _sampler = new();
    private readonly EtwSampler _etw = new();
    private readonly DispatcherTimer _timer;
    private readonly ProcessAggregator _aggregator;
    private readonly Dictionary<int, (ProcessIdentity Identity, double ExpiresAt)> _identityCache = new();

    private double _now;
    private double _lastConnectionRefresh = double.NegativeInfinity;

    public MainWindow()
    {
        InitializeComponent();

        _aggregator = new ProcessAggregator(ResolveIdentity);

        _sampler.SelectDefaultAdapter();
        AdapterLabel.Text = _sampler.Index == 0 ? "网速 · 未连接" : $"网速 · {_sampler.AdapterName}";

        if (_etw.IsRunning)
        {
            ProcessHeader.Visibility = Visibility.Visible;
            ProcessList.Visibility = Visibility.Visible;
            Footer.Text = "ETW 已连接 · 本地代理不计入 Top";
        }
        else
        {
            Banner.Visibility = Visibility.Visible;
            ConnectionHeader.Visibility = Visibility.Visible;
            ConnectionList.Visibility = Visibility.Visible;
            Footer.Text = _etw.Error is null
                ? "受限模式 · 进程按连接数排序"
                : $"ETW 启动失败：{_etw.Error}";
        }

        _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _timer.Tick += (_, _) => Tick();
        _timer.Start();

        Closed += (_, _) => _etw.Dispose();
    }

    private void Tick()
    {
        _now = Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency;

        (double down, double up) = _sampler.Sample();

        DownMain.Text = FormatMegabytes(down);
        DownMbps.Text = $"{down * 8 / 1_000_000:0.0} Mbps";
        UpMain.Text = FormatMegabytes(up);
        UpMbps.Text = $"{up * 8 / 1_000_000:0.0} Mbps";
        Trend.Push(down);

        if (!_etw.IsRunning)
        {
            // Connection counts come from the TCP table, which needs no elevation.
            if (_now - _lastConnectionRefresh >= 2)
            {
                _lastConnectionRefresh = _now;
                RefreshConnections();
            }

            return;
        }

        (List<ProcessRateRow> top, double external) = _aggregator.Update(_etw.Snapshot(), _now);

        ProcessList.ItemsSource = top;
        Footer.Text = $"ETW 载荷 {ProcessRates.FormatRate(external)}/s · 本地代理不计入";
    }

    private void RefreshConnections()
    {
        Dictionary<int, int> counts = ConnectionCounter.CountByPid();
        ConnectionList.ItemsSource = ConnectionCounter.BuildRows(counts, pid => ResolveIdentity(pid).Name);

        int total = counts.Values.Sum();
        ConnectionHeader.Text = total == 0 ? "进程连接" : $"进程连接 · {total} 条";
    }

    /// <summary>
    /// Resolves a PID to its merge key. Cached for a few seconds because opening
    /// every process once per tick is expensive; a reused PID shows up as a
    /// changed start time, and the aggregator drops that tick anyway.
    /// </summary>
    private ProcessIdentity ResolveIdentity(int pid)
    {
        if (_identityCache.TryGetValue(pid, out (ProcessIdentity Identity, double ExpiresAt) cached)
            && cached.ExpiresAt > _now)
        {
            return cached.Identity;
        }

        ProcessIdentity identity = LookupIdentity(pid);
        _identityCache[pid] = (identity, _now + 5);
        return identity;
    }

    private static ProcessIdentity LookupIdentity(int pid)
    {
        try
        {
            using Process process = Process.GetProcessById(pid);
            string name = $"{process.ProcessName}.exe";

            string key;
            try
            {
                key = process.MainModule?.FileName ?? name;
            }
            catch
            {
                // Protected process: the name alone still merges its instances.
                key = name;
            }

            return new ProcessIdentity(key.ToLowerInvariant(), name, process.StartTime.ToUniversalTime().Ticks);
        }
        catch
        {
            return new ProcessIdentity($"pid:{pid}", $"pid {pid}", 0);
        }
    }

    private static string FormatMegabytes(double bytesPerSecond)
    {
        double megabytes = bytesPerSecond / 1024d / 1024d;
        return megabytes.ToString(megabytes >= 100 ? "0" : megabytes >= 10 ? "0.0" : "0.00",
            CultureInfo.InvariantCulture);
    }

    private void OnWindowMouseDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left) return;

        try
        {
            DragMove();
        }
        catch (InvalidOperationException)
        {
            // Mouse was released between the event and the drag; nothing to do.
        }
    }

    private void OnCloseClick(object sender, RoutedEventArgs e) => Hide();

    private void OnRestartClick(object sender, RoutedEventArgs e)
    {
        if (Application.Current is App app) app.RestartElevated();
    }
}
