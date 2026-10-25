using System.Numerics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Hosting;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;

namespace XLabWinUI.Controls;

internal static class RoundedClip
{
    internal static void Attach(FrameworkElement element, float radius)
    {
        void Apply()
        {
            var visual = ElementCompositionPreview.GetElementVisual(element);
            var geometry = visual.Compositor.CreateRoundedRectangleGeometry();
            geometry.Size = new((float)element.ActualWidth, (float)element.ActualHeight);
            geometry.CornerRadius = new(radius, radius);
            visual.Clip = visual.Compositor.CreateGeometricClip(geometry);
        }
        element.SizeChanged += (_, _) => Apply();
        element.Loaded += (_, _) => Apply();
    }
}
internal sealed class CheckerboardSurface : Canvas
{
    internal CheckerboardSurface()
    {
        Background = Theme.Brush("#37373B");
        RoundedClip.Attach(this, 24);
        SizeChanged += (_, _) =>
        {
            Children.Clear();
            for (int row = 0; row * 38 < ActualHeight; ++row)
                for (int column = 0; column * 38 < ActualWidth; ++column)
                {
                    var cell = new Rectangle { Width = 38, Height = 38, Fill = Theme.Brush((row + column) % 2 == 0 ? "#434347" : "#37373B") };
                    SetLeft(cell, column * 38); SetTop(cell, row * 38); Children.Add(cell);
                }
        };
    }
}
internal sealed class CenteredWrapPanel : Panel
{
    private const double Cell = 84, Pitch = 96;
    protected override Size MeasureOverride(Size available)
    {
        foreach (UIElement child in Children) child.Measure(new(Cell, Cell));
        int columns = Math.Max(1, (int)Math.Floor((double.IsInfinity(available.Width) ? 1304 : available.Width + 12) / Pitch));
        return new(double.IsInfinity(available.Width) ? columns * Pitch - 12 : available.Width, Math.Max(0, Math.Ceiling((double)Children.Count / columns) * Pitch - 12));
    }
    protected override Size ArrangeOverride(Size final)
    {
        int columns = Math.Max(1, (int)Math.Floor((final.Width + 12) / Pitch));
        for (int i = 0; i < Children.Count; ++i)
        {
            int row = i / columns, count = Math.Min(columns, Children.Count - row * columns);
            Children[i].Arrange(new Rect((final.Width - (count * Pitch - 12)) / 2 + i % columns * Pitch, row * Pitch, Cell, Cell));
        }
        return final;
    }
}
