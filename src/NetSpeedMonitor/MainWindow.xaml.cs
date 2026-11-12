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
    private readonly Dictionary<int, string> _names = new();

    private Dictionary<int, EtwCounters> _lastEtw = new();
    private long _lastEtwTicks;

    public MainWindow()
    {
        InitializeComponent();

        _sampler.SelectDefaultAdapter();
        AdapterLabel.Text = _sampler.Index == 0 ? "网速 · 未连接" : $"网速 · {_sampler.AdapterName}";

        if (_etw.IsRunning)
        {
            ProcessHeader.Visibility = Visibility.Visible;
            ProcessList.Visibility = Visibility.Visible;
            _lastEtw = _etw.Snapshot();
            _lastEtwTicks = Stopwatch.GetTimestamp();
            Footer.Text = "ETW 已连接 · 本地代理不计入 Top";
        }
        else
        {
            Banner.Visibility = Visibility.Visible;
            Footer.Text = _etw.Error is null ? "受限模式：仅显示网卡总量" : $"ETW 启动失败：{_etw.Error}";
        }

        _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _timer.Tick += (_, _) => Tick();
        _timer.Start();

        Closed += (_, _) => _etw.Dispose();
    }

    private void Tick()
    {
        (double down, double up) = _sampler.Sample();

        DownMain.Text = FormatMegabytes(down);
        DownMbps.Text = $"{down * 8 / 1_000_000:0.0} Mbps";
        UpMain.Text = FormatMegabytes(up);
        UpMbps.Text = $"{up * 8 / 1_000_000:0.0} Mbps";
        Trend.Push(down);

        if (!_etw.IsRunning) return;

        Dictionary<int, EtwCounters> snapshot = _etw.Snapshot();
        long now = Stopwatch.GetTimestamp();
        double seconds = (now - _lastEtwTicks) / (double)Stopwatch.Frequency;

        if (seconds <= 0 || seconds > 5)
        {
            _lastEtw = snapshot;
            _lastEtwTicks = now;
            return;
        }

        (List<ProcessRateRow> top, double external) = ProcessRates.Compute(_lastEtw, snapshot, seconds, ResolveName);
        _lastEtw = snapshot;
        _lastEtwTicks = now;

        ProcessList.ItemsSource = top;
        Footer.Text = $"ETW 外网 {ProcessRates.FormatRate(external)}/s · 本地代理不计入";
    }

    private string ResolveName(int pid)
    {
        if (_names.TryGetValue(pid, out string? cached)) return cached;

        string name;
        try
        {
            name = $"{Process.GetProcessById(pid).ProcessName}.exe";
        }
        catch
        {
            name = $"pid {pid}";
        }

        _names[pid] = name;
        return name;
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
