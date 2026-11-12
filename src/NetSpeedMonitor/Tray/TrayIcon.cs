using Drawing = System.Drawing;
using WinForms = System.Windows.Forms;

namespace NetSpeedMonitor.Tray;

public sealed class TrayIcon : IDisposable
{
    private readonly WinForms.NotifyIcon _notifyIcon;
    private readonly Drawing.Icon _icon;
    private bool _disposed;

    public event Action? ToggleRequested;
    public event Action? ExitRequested;

    public TrayIcon()
    {
        _icon = CreateIcon();

        var menu = new WinForms.ContextMenuStrip();
        menu.Items.Add("退出", null, (_, _) => ExitRequested?.Invoke());

        _notifyIcon = new WinForms.NotifyIcon
        {
            Icon = _icon,
            Text = "NetSpeedMonitor",
            Visible = true,
            ContextMenuStrip = menu,
        };

        _notifyIcon.MouseClick += (_, e) =>
        {
            if (e.Button == WinForms.MouseButtons.Left) ToggleRequested?.Invoke();
        };
    }

    private static Drawing.Icon CreateIcon()
    {
        using var bitmap = new Drawing.Bitmap(32, 32);
        using (Drawing.Graphics graphics = Drawing.Graphics.FromImage(bitmap))
        {
            graphics.SmoothingMode = Drawing.Drawing2D.SmoothingMode.AntiAlias;
            graphics.Clear(Drawing.Color.Transparent);

            using var background = new Drawing.SolidBrush(Drawing.Color.FromArgb(255, 20, 22, 26));
            graphics.FillEllipse(background, 0, 0, 31, 31);

            using var accent = new Drawing.Pen(Drawing.Color.FromArgb(255, 76, 194, 255), 3f)
            {
                StartCap = Drawing.Drawing2D.LineCap.Round,
                EndCap = Drawing.Drawing2D.LineCap.Round,
            };
            graphics.DrawLine(accent, 11, 9, 11, 23);
            graphics.DrawLine(accent, 21, 9, 21, 23);
            graphics.DrawLine(accent, 7, 13, 11, 9);
            graphics.DrawLine(accent, 15, 13, 11, 9);
        }

        return Drawing.Icon.FromHandle(bitmap.GetHicon());
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _notifyIcon.Visible = false;
        _notifyIcon.Dispose();
        _icon.Dispose();
    }
}
