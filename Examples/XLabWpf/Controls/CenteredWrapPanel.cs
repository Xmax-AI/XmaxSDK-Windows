using System.Windows;
using System.Windows.Controls;

namespace XLabWpf.Controls;

public sealed class CenteredWrapPanel : Panel
{
    private const double Cell = 84, Pitch = 96;
    protected override Size MeasureOverride(Size available)
    {
        foreach (UIElement child in InternalChildren) child.Measure(new(Cell, Cell));
        int columns = Math.Max(1, (int)Math.Floor((double.IsInfinity(available.Width) ? 1304 : available.Width + 12) / Pitch));
        return new(double.IsInfinity(available.Width) ? columns * Pitch - 12 : available.Width,
            Math.Max(0, Math.Ceiling((double)InternalChildren.Count / columns) * Pitch - 12));
    }
    protected override Size ArrangeOverride(Size final)
    {
        int columns = Math.Max(1, (int)Math.Floor((final.Width + 12) / Pitch));
        for (int i = 0; i < InternalChildren.Count; ++i)
        {
            int row = i / columns, count = Math.Min(columns, InternalChildren.Count - row * columns);
            double start = (final.Width - (count * Pitch - 12)) / 2;
            InternalChildren[i].Arrange(new Rect(start + i % columns * Pitch, row * Pitch, Cell, Cell));
        }
        return final;
    }
}
