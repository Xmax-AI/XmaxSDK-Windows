using System.Windows;
using System.Windows.Media;

namespace XLabWpf.Controls;

public sealed class CheckerboardSurface : FrameworkElement
{
    private static readonly Brush Light = new SolidColorBrush(Color.FromRgb(67, 67, 71));
    private static readonly Brush Dark = new SolidColorBrush(Color.FromRgb(55, 55, 59));
    public CheckerboardSurface() => RenderOptions.SetEdgeMode(this, EdgeMode.Aliased);
    protected override void OnRender(DrawingContext dc)
    {
        dc.PushClip(new RectangleGeometry(new Rect(RenderSize), 24, 24));
        for (int row = 0; row * 38 < ActualHeight; ++row)
            for (int column = 0; column * 38 < ActualWidth; ++column)
                dc.DrawRectangle((row + column) % 2 == 0 ? Light : Dark, null, new Rect(column * 38, row * 38, 38, 38));
        dc.Pop();
    }
}
