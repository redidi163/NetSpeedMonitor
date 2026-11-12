using System.ComponentModel;
using System.Diagnostics;
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

        if (e.Args.Length >= 2 && e.Args[0] == "--probe-etw")
        {
            string output = e.Args.Length >= 3
                ? e.Args[2]
                : Path.Combine(Path.GetTempPath(), "netspeed-etw.csv");
            Services.ProbeRunner.RunEtw(
                int.Parse(e.Args[1], CultureInfo.InvariantCulture),
                output,
                e.Args.Length >= 4 && e.Args[3] == "load");
            Shutdown();
            return;
        }

        _mutex = AcquireMutex(TimeSpan.FromSeconds(4));
        if (_mutex is null)
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

    private static Mutex? AcquireMutex(TimeSpan timeout)
    {
        DateTime deadline = DateTime.UtcNow + timeout;

        while (true)
        {
            var mutex = new Mutex(true, @"Local\NetSpeedMonitor.SingleInstance", out bool created);
            if (created) return mutex;

            mutex.Dispose();
            if (DateTime.UtcNow >= deadline) return null;
            Thread.Sleep(150);
        }
    }

    /// <summary>
    /// Starts a new elevated copy and lets this one exit. The new process waits
    /// up to 4 seconds for the single-instance mutex, which covers the gap.
    /// </summary>
    public void RestartElevated()
    {
        string? executable = Environment.ProcessPath;
        if (executable is null) return;

        try
        {
            Process.Start(new ProcessStartInfo(executable)
            {
                UseShellExecute = true,
                Verb = "runas",
            });
        }
        catch (Win32Exception)
        {
            // UAC cancelled; stay in restricted mode.
            return;
        }

        _tray?.Dispose();
        _mutex?.Dispose();
        _mutex = null;
        _window?.Close();
        Shutdown();
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
