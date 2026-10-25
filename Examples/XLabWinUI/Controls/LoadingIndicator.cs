using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;

namespace XLabWinUI.Controls;

// Keep the toolbar spinner in XAML primitives. The default ProgressRing's Lottie
// template crashes during first layout in the unpackaged x64 Demo on Windows ARM.
internal sealed class LoadingIndicator : Grid
{
    private readonly Microsoft.UI.Xaml.Shapes.Path arc = new()
    {
        StrokeThickness = 1.8, StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round,
        Data = new PathGeometry { Figures = { new PathFigure { StartPoint = new(7, 1.5),
            Segments = { new ArcSegment { Point = new(1.5, 7), Size = new(5.5, 5.5), IsLargeArc = true, SweepDirection = SweepDirection.Clockwise } } } } }
    };
    private readonly Storyboard rotation = new();
    private bool active;
    internal Brush? Foreground { get => arc.Stroke; set => arc.Stroke = value; }
    internal bool IsActive
    {
        get => active;
        set { if (active == value) return; active = value; UpdateAnimation(); }
    }
    internal LoadingIndicator()
    {
        Width = Height = 14; Visibility = Visibility.Collapsed; IsHitTestVisible = false;
        VerticalAlignment = VerticalAlignment.Center;
        var transform = new RotateTransform { CenterX = 7, CenterY = 7 };
        arc.RenderTransform = transform; Children.Add(arc);
        var animation = new DoubleAnimation { From = 0, To = 360, Duration = new(TimeSpan.FromMilliseconds(800)),
            RepeatBehavior = RepeatBehavior.Forever, EnableDependentAnimation = true };
        Storyboard.SetTarget(animation, transform); Storyboard.SetTargetProperty(animation, "Angle");
        rotation.Children.Add(animation);
        Loaded += (_, _) => UpdateAnimation();
        Unloaded += (_, _) => rotation.Stop();
    }
    private void UpdateAnimation()
    {
        rotation.Stop();
        if (active && IsLoaded) rotation.Begin();
    }
}
