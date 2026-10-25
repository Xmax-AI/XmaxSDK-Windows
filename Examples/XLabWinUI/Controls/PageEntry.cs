using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;
using Windows.UI.ViewManagement;

namespace XLabWinUI.Controls;

internal static class PageEntry
{
    private static readonly Dictionary<FrameworkElement, Storyboard> Running = [];
    internal static void Reveal(FrameworkElement page)
    {
        if (Running.Remove(page, out var previous)) previous.Stop();
        page.Opacity = 1;
        var shift = new TranslateTransform(); page.RenderTransform = shift;
        if (!new UISettings().AnimationsEnabled) return;
        var storyboard = new Storyboard { FillBehavior = FillBehavior.Stop };
        var fade = Animation(0, 1); Storyboard.SetTarget(fade, page); Storyboard.SetTargetProperty(fade, "Opacity");
        var move = Animation(14, 0); Storyboard.SetTarget(move, shift); Storyboard.SetTargetProperty(move, "Y");
        storyboard.Children.Add(fade); storyboard.Children.Add(move);
        storyboard.Completed += (_, _) => { if (Running.TryGetValue(page, out var current) && current == storyboard) Running.Remove(page); };
        Running[page] = storyboard;
        storyboard.Begin();
    }
    private static DoubleAnimationUsingKeyFrames Animation(double from, double to) => new()
    {
        Duration = new(TimeSpan.FromMilliseconds(450)), EnableDependentAnimation = true,
        KeyFrames = { new LinearDoubleKeyFrame { Value = from, KeyTime = KeyTime.FromTimeSpan(TimeSpan.Zero) },
            new SplineDoubleKeyFrame { Value = to, KeyTime = KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(450)), KeySpline = new() { ControlPoint1 = new(0, 0), ControlPoint2 = new(.58, 1) } } }
    };
}
