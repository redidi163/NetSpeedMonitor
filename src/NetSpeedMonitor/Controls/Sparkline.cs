using System.Windows;
using System.Windows.Media;

namespace NetSpeedMonitor.Controls;

/// <summary>
/// 60-slot trend line. Values are pushed once per sample; the newest value is
/// always at the right edge and empty slots stay blank on the left.
/// </summary>
public sealed class Sparkline : FrameworkElement
{
    private const int Capacity = 60;

    private readonly List<double> _values = new(Capacity);

    public Brush Stroke { get; set; } = Brushes.DeepSkyBlue;

    public void Push(double bytesPerSecond)
    {
        _values.Add(bytesPerSecond);
        if (_values.Count > Capacity) _values.RemoveAt(0);
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        double width = ActualWidth;
        double height = ActualHeight;
        if (width <= 0 || height <= 0 || _values.Count < 2) return;

        double max = Math.Max(_values.Max(), 1);
        var pen = new Pen(Stroke, 1.5);
        pen.Freeze();

        var geometry = new StreamGeometry();
        using (StreamGeometryContext ctx = geometry.Open())
        {
            int offset = Capacity - _values.Count;
            for (int i = 0; i < _values.Count; i++)
            {
                double x = width * (offset + i) / (Capacity - 1);
                double y = height - 2 - (_values[i] / max) * (height - 4);
                var point = new Point(x, y);
                if (i == 0) ctx.BeginFigure(point, false, false);
                else ctx.LineTo(point, true, false);
            }
        }

        geometry.Freeze();
        dc.DrawGeometry(null, pen, geometry);
    }
}
