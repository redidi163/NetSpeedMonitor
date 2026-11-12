using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using NetSpeedMonitor.Services;

namespace NetSpeedMonitor;

public partial class MainWindow : Window
{
    private readonly AdapterSampler _sampler = new();
    private readonly DispatcherTimer _timer;

    public MainWindow()
    {
        InitializeComponent();

        _sampler.SelectDefaultAdapter();
        AdapterLabel.Text = _sampler.Index == 0 ? "网速 · 未连接" : $"网速 · {_sampler.AdapterName}";

        _timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _timer.Tick += (_, _) => Tick();
        _timer.Start();
    }

    private void Tick()
    {
        (double down, double up) = _sampler.Sample();

        DownMain.Text = FormatMegabytes(down);
        DownMbps.Text = $"{down * 8 / 1_000_000:0.0} Mbps";
        UpMain.Text = FormatMegabytes(up);
        UpMbps.Text = $"{up * 8 / 1_000_000:0.0} Mbps";
        Trend.Push(down);
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
}
