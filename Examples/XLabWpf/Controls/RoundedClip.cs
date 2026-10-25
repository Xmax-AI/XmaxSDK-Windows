using System.Windows;
using System.Windows.Media;

namespace XLabWpf.Controls;

public static class RoundedClip
{
    public static readonly DependencyProperty RadiusProperty = DependencyProperty.RegisterAttached("Radius", typeof(double), typeof(RoundedClip), new PropertyMetadata(0d, Changed));
    public static double GetRadius(DependencyObject value) => (double)value.GetValue(RadiusProperty);
    public static void SetRadius(DependencyObject value, double radius) => value.SetValue(RadiusProperty, radius);
    private static void Changed(DependencyObject value, DependencyPropertyChangedEventArgs args)
    {
        if (value is not FrameworkElement element) return;
        element.SizeChanged -= Resize;
        element.SizeChanged += Resize;
        Apply(element);
    }
    private static void Resize(object sender, SizeChangedEventArgs args) => Apply((FrameworkElement)sender);
    private static void Apply(FrameworkElement element) => element.Clip = new RectangleGeometry(new Rect(0, 0, element.ActualWidth, element.ActualHeight), GetRadius(element), GetRadius(element));
}
