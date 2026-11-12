using System.Globalization;
using System.IO;
using System.Threading;
using System.Windows;

namespace NetSpeedMonitor;

public partial class App : Application
{
    private Mutex? _mutex;
    private Tray.TrayIcon? _tray;
    private MainWindow? _window;

    protected override void OnStartup(StartupEventArgs e)
    {
        if (e.Args.Length >= 2 && e.Args[0] == "--probe")
        {
            string output = e.Args.Length >= 3
                ? e.Args[2]
                : Path.Combine(Path.GetTempPath(), "netspeed-probe.csv");
            Services.ProbeRunner.Run(int.Parse(e.Args[1], CultureInfo.InvariantCulture), output);
            Shutdown();
            return;
        }

        _mutex = new Mutex(true, @"Local\NetSpeedMonitor.SingleInstance", out bool created);
        if (!created)
        {
            Shutdown();
            return;
        }

        base.OnStartup(e);

        _window = new MainWindow();
        _tray = new Tray.TrayIcon();
        _tray.ToggleRequested += ToggleWindow;
        _tray.ExitRequested += ExitApp;
        _window.Show();
    }

    private void ToggleWindow()
    {
        if (_window is null) return;

        if (_window.IsVisible)
        {
            _window.Hide();
        }
        else
        {
            _window.Show();
            _window.Activate();
        }
    }

    private void ExitApp()
    {
        _tray?.Dispose();
        _window?.Close();
        Shutdown();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _tray?.Dispose();
        _mutex?.Dispose();
        base.OnExit(e);
    }
}
