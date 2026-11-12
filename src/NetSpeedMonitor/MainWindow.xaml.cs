using System.Diagnostics;
using System.Globalization;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using NetSpeedMonitor.Interop;
using NetSpeedMonitor.Services;

namespace NetSpeedMonitor;

public partial class MainWindow : Window
{
    private readonly AdapterSampler _sampler = new();
    private readonly DispatcherTimer _timer;
    private readonly ProcessAggregator _aggregator;
    private readonly Settings _settings;
    private readonly Dictionary<int, (ProcessIdentity Identity, double ExpiresAt)> _identityCache = new();

    private EtwSampler _etw = new();
    private bool _etwRunning;
    private bool _offline;
    private double _now;
    private double _lastConnectionRefresh = double.NegativeInfinity;
    private double _lastRouteCheck = double.NegativeInfinity;
    private double _lastEtwRebuild = double.NegativeInfinity;
    private bool _syncingPicker;
    private string _pickerSignature = "";

    public MainWindow()
    {
        InitializeComponent();

        _settings = Settings.Load();
        _aggregator = new ProcessAggregator(ResolveIdentity);

        ApplyAdapterPreference();
        SyncAdapterChoices(force: true);
        ApplyEtwState();

        _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _timer.Tick += (_, _) => Tick();
        _timer.Start();

        Closed += OnClosed;
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);

        // SystemEvents would work too, but its listener window is message-only,
        // which makes the resume path impossible to exercise without actually
        // suspending the machine. The app window gets the same broadcast.
        HwndSource.FromHwnd(new WindowInteropHelper(this).Handle)?.AddHook(WndProc);
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg != NativeMethods.WmPowerBroadcast) return IntPtr.Zero;

        int reason = wParam.ToInt32();
        if (reason is not (NativeMethods.PbtApmResumeAutomatic or NativeMethods.PbtApmResumeSuspend))
        {
            return IntPtr.Zero;
        }

        // Resume sends both reasons back to back; rebuilding twice would tear
        // down the session that was just created.
        double now = Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency;
        if (now - _lastEtwRebuild < 3) return IntPtr.Zero;

        _lastEtwRebuild = now;
        RebuildEtw();
        return IntPtr.Zero;
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        _timer.Stop();
        _etw.Dispose();
    }

    /// <summary>
    /// Re-reads the pinned interface (or the default route) and hands it to the
    /// sampler. Selecting resets the counter baseline, so the first sample after
    /// a switch reports 0 instead of a delta spanning two interfaces.
    /// </summary>
    private void ApplyAdapterPreference()
    {
        if (_settings.AdapterIndex == 0) _sampler.FollowDefaultRoute();
        else _sampler.Select(_settings.AdapterIndex);
    }

    /// <summary>
    /// Rebuilds the dropdown when the interface list or the default route
    /// changed. Skipped while the user has it open so their click is not eaten.
    /// </summary>
    private void SyncAdapterChoices(bool force = false)
    {
        if (!force && AdapterPicker.IsDropDownOpen) return;

        List<AdapterOption> options = AdapterCatalog.List();
        uint routeIndex = DefaultRoute.GetDefaultInterfaceIndex();
        string routeName = routeIndex == 0
            ? "未连接"
            : AdapterCatalog.NameOf(routeIndex) ?? $"接口 {routeIndex}";
        string autoLabel = $"自动 · {routeName}";

        string signature = autoLabel + "#"
            + string.Join("|", options.Select(option => $"{option.Index}:{option.Name}:{option.Up}"));

        if (!force && signature == _pickerSignature) return;
        _pickerSignature = signature;

        _syncingPicker = true;
        try
        {
            AdapterPicker.Items.Clear();
            AdapterPicker.Items.Add(new ComboBoxItem { Content = autoLabel, Tag = 0u });

            foreach (AdapterOption option in options)
            {
                AdapterPicker.Items.Add(new ComboBoxItem { Content = option.Label, Tag = option.Index });
            }

            int selected = IndexOfAdapter(_settings.AdapterIndex);
            if (selected < 0)
            {
                // Pinned interface is gone (unplugged dongle): keep the choice
                // visible and report zero instead of silently switching to auto.
                AdapterPicker.Items.Add(new ComboBoxItem
                {
                    Content = $"接口 {_settings.AdapterIndex}（已移除）",
                    Tag = _settings.AdapterIndex,
                });
                selected = AdapterPicker.Items.Count - 1;
            }

            AdapterPicker.SelectedIndex = selected;

            // Screen readers (and the acceptance script) need the effective
            // interface as the control name; the ContentPresenter is not a
            // TextBlock, so UIA cannot read it otherwise.
            string effective = _settings.AdapterIndex == 0
                ? $"自动 · {_sampler.AdapterName}"
                : _sampler.AdapterName;
            AutomationProperties.SetName(AdapterPicker, $"统计接口：{effective}");
        }
        finally
        {
            _syncingPicker = false;
        }
    }

    private int IndexOfAdapter(uint index)
    {
        for (int i = 0; i < AdapterPicker.Items.Count; i++)
        {
            if (AdapterPicker.Items[i] is ComboBoxItem item
                && item.Tag is uint tag
                && tag == index)
            {
                return i;
            }
        }

        return -1;
    }

    private void OnAdapterSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_syncingPicker) return;
        if (AdapterPicker.SelectedItem is not ComboBoxItem item) return;
        if (item.Tag is not uint index) return;

        _settings.AdapterIndex = index;
        _settings.Save();

        ApplyAdapterPreference();
        _sampler.Sample(); // take the baseline now, so the next tick already reports the new interface
        SyncAdapterChoices();
    }

    private void ApplyEtwState()
    {
        bool running = _etw.IsRunning;
        _etwRunning = running;

        Banner.Visibility = running ? Visibility.Collapsed : Visibility.Visible;
        ProcessHeader.Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        ProcessList.Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        ConnectionHeader.Visibility = running ? Visibility.Collapsed : Visibility.Visible;
        ConnectionList.Visibility = running ? Visibility.Collapsed : Visibility.Visible;

        Footer.Text = running
            ? "ETW 已连接 · 本地代理不计入 Top"
            : _etw.Error is null
                ? "受限模式 · 进程按连接数排序"
                : $"ETW 启动失败：{_etw.Error}";
    }

    private void RebuildEtw()
    {
        _etw.Dispose();
        _etw = new EtwSampler();

        _aggregator.Reset();
        _sampler.ResetBaseline();
        _lastConnectionRefresh = double.NegativeInfinity;
        _lastRouteCheck = double.NegativeInfinity;

        ApplyEtwState();
    }

    private void Tick()
    {
        _now = Stopwatch.GetTimestamp() / (double)Stopwatch.Frequency;

        if (_now - _lastRouteCheck >= 5)
        {
            _lastRouteCheck = _now;
            if (_settings.AdapterIndex == 0) _sampler.FollowDefaultRoute();
            SyncAdapterChoices();
        }

        (double down, double up) = _sampler.Sample();

        DownMain.Text = FormatMegabytes(down);
        DownMbps.Text = $"{down * 8 / 1_000_000:0.0} Mbps";
        UpMain.Text = FormatMegabytes(up);
        UpMbps.Text = $"{up * 8 / 1_000_000:0.0} Mbps";
        Trend.Push(down);

        // No default route: the totals are already zero, and any rows left over
        // are loopback chatter between local processes, which would read as
        // "still downloading" right after the cable is pulled.
        bool offline = _sampler.Index == 0;
        if (offline != _offline)
        {
            _offline = offline;
            _aggregator.Reset();
            if (offline) ProcessList.ItemsSource = Array.Empty<ProcessRateRow>();
        }

        if (_etw.IsRunning != _etwRunning) ApplyEtwState();

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

        if (offline)
        {
            Footer.Text = "断网 · 等待默认路由恢复";
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

        // The adapter dropdown lives in the drag area; without this the press
        // that opens it would also start a window drag.
        if (e.OriginalSource is DependencyObject source && IsInside<ComboBox>(source)) return;

        try
        {
            DragMove();
        }
        catch (InvalidOperationException)
        {
            // Mouse was released between the event and the drag; nothing to do.
        }
    }

    private static bool IsInside<T>(DependencyObject? node) where T : DependencyObject
    {
        while (node is not null)
        {
            if (node is T) return true;

            node = node is Visual or System.Windows.Media.Media3D.Visual3D
                ? VisualTreeHelper.GetParent(node)
                : LogicalTreeHelper.GetParent(node);
        }

        return false;
    }

    private void OnCloseClick(object sender, RoutedEventArgs e) => Hide();

    private void OnRestartClick(object sender, RoutedEventArgs e)
    {
        if (Application.Current is App app) app.RestartElevated();
    }
}
