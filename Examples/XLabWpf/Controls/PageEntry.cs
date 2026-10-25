using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace XLabWpf.Controls;

// Matches Qt PageEntry/macOS animateEntry: 450 ms, 14 DIP, CSS ease-out.
public static class PageEntry
{
    public static readonly DependencyProperty EnabledProperty = DependencyProperty.RegisterAttached(
        "Enabled", typeof(bool), typeof(PageEntry), new PropertyMetadata(false, EnabledChanged));
    private static readonly DependencyProperty PendingProperty = DependencyProperty.RegisterAttached(
        "Pending", typeof(DispatcherOperation), typeof(PageEntry));
    public static bool GetEnabled(DependencyObject target) => (bool)target.GetValue(EnabledProperty);
    public static void SetEnabled(DependencyObject target, bool value) => target.SetValue(EnabledProperty, value);

    private static void EnabledChanged(DependencyObject target, DependencyPropertyChangedEventArgs args)
    {
        if (target is not FrameworkElement page) return;
        if ((bool)args.NewValue)
        {
            page.RenderTransform = new TranslateTransform();
            page.Loaded += Loaded;
            page.Unloaded += Unloaded;
            page.IsVisibleChanged += VisibilityChanged;
            if (page.IsLoaded) Reveal(page);
        }
        else
        {
            page.Loaded -= Loaded;
            page.Unloaded -= Unloaded;
            page.IsVisibleChanged -= VisibilityChanged;
            Reset(page);
        }
    }
    private static void Loaded(object sender, RoutedEventArgs args) => Reveal((FrameworkElement)sender);
    private static void Unloaded(object sender, RoutedEventArgs args) => Reset((FrameworkElement)sender);
    private static void VisibilityChanged(object sender, DependencyPropertyChangedEventArgs args) => Reveal((FrameworkElement)sender);
    private static void Reset(FrameworkElement page)
    {
        (page.GetValue(PendingProperty) as DispatcherOperation)?.Abort();
        page.ClearValue(PendingProperty);
        page.BeginAnimation(UIElement.OpacityProperty, null);
        if (page.RenderTransform is TranslateTransform shift) shift.BeginAnimation(TranslateTransform.YProperty, null);
    }
    private static void Reveal(FrameworkElement page)
    {
        Reset(page);
        if (!page.IsVisible) return;
        page.SetValue(PendingProperty, page.Dispatcher.InvokeAsync(() =>
        {
            page.ClearValue(PendingProperty);
            if (!page.IsLoaded || !page.IsVisible || !SystemParameters.ClientAreaAnimation) return;
            page.BeginAnimation(UIElement.OpacityProperty, Animation(0, 1));
            ((TranslateTransform)page.RenderTransform).BeginAnimation(TranslateTransform.YProperty, Animation(14, 0));
        }, DispatcherPriority.Loaded));
    }
    private static DoubleAnimationUsingKeyFrames Animation(double from, double to) => new()
    {
        Duration = TimeSpan.FromMilliseconds(450), FillBehavior = FillBehavior.Stop,
        KeyFrames =
        {
            new LinearDoubleKeyFrame(from, KeyTime.FromTimeSpan(TimeSpan.Zero)),
            new SplineDoubleKeyFrame(to, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(450)), new KeySpline(0, 0, .58, 1))
        }
    };
}
