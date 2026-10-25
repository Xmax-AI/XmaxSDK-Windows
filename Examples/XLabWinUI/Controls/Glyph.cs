using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;

namespace XLabWinUI.Controls;

internal sealed class Glyph : Grid
{
    private readonly Canvas canvas = new() { Width = 24, Height = 24 };
    public static readonly DependencyProperty KindProperty = DependencyProperty.Register(nameof(Kind), typeof(string), typeof(Glyph), new PropertyMetadata("", Changed));
    public static readonly DependencyProperty ForegroundProperty = DependencyProperty.Register(nameof(Foreground), typeof(Brush), typeof(Glyph), new PropertyMetadata(null, Changed));
    public string Kind { get => (string)GetValue(KindProperty); set => SetValue(KindProperty, value); }
    public Brush Foreground { get => (Brush)GetValue(ForegroundProperty); set => SetValue(ForegroundProperty, value); }
    private static readonly Dictionary<string, string> Paths = new()
    {
        ["camera"] = "M5,6 L13,6 Q16,6 16,9 L16,15 Q16,18 13,18 L5,18 Q2,18 2,15 L2,9 Q2,6 5,6 M16,10 L22,6.5 L22,17.5 L16,14",
        ["microphone"] = "M9,5 Q9,2 12,2 Q15,2 15,5 L15,11 Q15,14 12,14 Q9,14 9,11 Z M5,10 L5,12 C5,21 19,21 19,12 L19,10 M12,19 L12,22 M8,22 L16,22",
        ["key"] = "M11.4,11.6 L21,2 M15.5,7.5 L18.5,10.5 L22,7 L19,4",
        ["chevron"] = "M6,9 L12,15 L18,9",
        ["statistics"] = "M4,20 L4,11 M12,20 L12,4 M20,20 L20,14",
        ["volume"] = "M11,5 L5,9 L2,9 L2,15 L5,15 L11,19 Z M15,8 C19,10 19,14 15,16 M18,5 C24,9 24,15 18,19",
        ["muted"] = "M11,5 L5,9 L2,9 L2,15 L5,15 L11,19 Z M16,9 L22,15 M22,9 L16,15",
        ["plus"] = "M12,4 L12,20 M4,12 L20,12",
        ["arrow"] = "M4,12 L20,12 M14,6 L20,12 L14,18",
        ["close"] = "M6,6 L18,18 M18,6 L6,18",
        ["warning"] = "M12,7 L12,13 M12,16 L12,17",
        ["globe"] = "M2,12 L22,12 M12,2 C21.4,10.9 21.4,13.1 12,22 C2.6,13.1 2.6,10.9 12,2",
        ["socialX"] = "M4,3 L9,3 L21,21 L16,21 Z M20,3 L4,21",
        ["wechat"] = "M3,7 C5,1 18,2 19,8 C26,12 22,22 15,20 L12,22 L12,19 C8,19 6,17 6,14 L3,16 L4,12 C1,10 2,8 3,7",
        ["bolt"] = "M13,2 L5,14 L11,14 L10,22 L19,9 L13,9 Z",
    };
    public Glyph() { Children.Add(new Viewbox { Child = canvas }); IsHitTestVisible = false; }
    private static void Changed(DependencyObject sender, DependencyPropertyChangedEventArgs args) => ((Glyph)sender).Draw();
    private void Draw()
    {
        canvas.Children.Clear();
        if (Foreground is null) return;
        if (Kind is "key" or "warning" or "globe" or "record")
        {
            double radius = Kind == "key" ? 5.5 : Kind == "warning" ? 9 : Kind == "record" ? 8 : 10;
            var circle = new Ellipse { Width = radius * 2, Height = radius * 2, Stroke = Foreground, StrokeThickness = 1.8, Fill = Kind == "record" ? Foreground : null };
            Canvas.SetLeft(circle, (Kind == "key" ? 7.5 : 12) - radius); Canvas.SetTop(circle, (Kind == "key" ? 15.5 : 12) - radius); canvas.Children.Add(circle);
        }
        if (Kind == "stop")
        {
            var square = new Rectangle { Width = 10, Height = 10, RadiusX = 1, RadiusY = 1, Fill = Foreground };
            Canvas.SetLeft(square, 7); Canvas.SetTop(square, 7); canvas.Children.Add(square);
        }
        else if (Paths.TryGetValue(Kind, out string? data))
        {
            var path = (Microsoft.UI.Xaml.Shapes.Path)XamlReader.Load($"<Path xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Data='{data}'/>");
            path.Stroke = Foreground; path.StrokeThickness = 1.8; path.StrokeStartLineCap = path.StrokeEndLineCap = PenLineCap.Round; path.StrokeLineJoin = PenLineJoin.Round;
            canvas.Children.Add(path);
        }
    }
}
