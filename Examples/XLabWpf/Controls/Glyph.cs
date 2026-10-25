using System.Windows;
using System.Windows.Media;

namespace XLabWpf.Controls;

public sealed class Glyph : FrameworkElement
{
    public static readonly DependencyProperty KindProperty = DependencyProperty.Register(nameof(Kind), typeof(string), typeof(Glyph), new FrameworkPropertyMetadata("camera", FrameworkPropertyMetadataOptions.AffectsRender));
    public static readonly DependencyProperty ForegroundProperty = DependencyProperty.Register(nameof(Foreground), typeof(Brush), typeof(Glyph), new FrameworkPropertyMetadata(Brushes.Black, FrameworkPropertyMetadataOptions.AffectsRender));
    public string Kind { get => (string)GetValue(KindProperty); set => SetValue(KindProperty, value); }
    public Brush Foreground { get => (Brush)GetValue(ForegroundProperty); set => SetValue(ForegroundProperty, value); }
    private static readonly Dictionary<string, Geometry> Paths = new()
    {
        ["camera"] = Geometry.Parse("M5,6 L13,6 Q16,6 16,9 L16,15 Q16,18 13,18 L5,18 Q2,18 2,15 L2,9 Q2,6 5,6 M16,10 L22,6.5 L22,17.5 L16,14"),
        ["microphone"] = Geometry.Parse("M9,5 Q9,2 12,2 Q15,2 15,5 L15,11 Q15,14 12,14 Q9,14 9,11 Z M5,10 L5,12 C5,21 19,21 19,12 L19,10 M12,19 L12,22 M8,22 L16,22"),
        ["key"] = Geometry.Parse("M11.4,11.6 L21,2 M15.5,7.5 L18.5,10.5 L22,7 L19,4"),
        ["chevron"] = Geometry.Parse("M6,9 L12,15 L18,9"),
        ["statistics"] = Geometry.Parse("M4,20 L4,11 M12,20 L12,4 M20,20 L20,14"),
        ["volume"] = Geometry.Parse("M11,5 L5,9 L2,9 L2,15 L5,15 L11,19 Z M15,8 C19,10 19,14 15,16 M18,5 C24,9 24,15 18,19"),
        ["muted"] = Geometry.Parse("M11,5 L5,9 L2,9 L2,15 L5,15 L11,19 Z M16,9 L22,15 M22,9 L16,15"),
        ["plus"] = Geometry.Parse("M12,4 L12,20 M4,12 L20,12"),
        ["arrow"] = Geometry.Parse("M4,12 L20,12 M14,6 L20,12 L14,18"),
        ["close"] = Geometry.Parse("M6,6 L18,18 M18,6 L6,18"),
        ["warning"] = Geometry.Parse("M12,7 L12,13 M12,16 L12,17"),
        ["globe"] = Geometry.Parse("M2,12 L22,12 M12,2 C21.4,10.9 21.4,13.1 12,22 C2.6,13.1 2.6,10.9 12,2"),
        ["socialX"] = Geometry.Parse("M4,3 L9,3 L21,21 L16,21 Z M20,3 L4,21"),
        ["wechat"] = Geometry.Parse("M3,7 C5,1 18,2 19,8 C26,12 22,22 15,20 L12,22 L12,19 C8,19 6,17 6,14 L3,16 L4,12 C1,10 2,8 3,7"),
        ["bolt"] = Geometry.Parse("M13,2 L5,14 L11,14 L10,22 L19,9 L13,9 Z")
    };
    protected override void OnRender(DrawingContext dc)
    {
        dc.PushTransform(new ScaleTransform(ActualWidth / 24, ActualHeight / 24));
        var pen = new Pen(Foreground, 1.8) { StartLineCap = PenLineCap.Round, EndLineCap = PenLineCap.Round, LineJoin = PenLineJoin.Round };
        if (Kind is "key" or "warning" or "globe") dc.DrawEllipse(null, pen, Kind == "key" ? new(7.5, 15.5) : new(12, 12), Kind == "key" ? 5.5 : Kind == "warning" ? 9 : 10, Kind == "key" ? 5.5 : Kind == "warning" ? 9 : 10);
        if (Kind == "record") dc.DrawEllipse(Foreground, null, new(12, 12), 8, 8);
        else if (Kind == "stop") dc.DrawRoundedRectangle(Foreground, null, new Rect(7, 7, 10, 10), 1, 1);
        else if (Paths.TryGetValue(Kind, out var path)) dc.DrawGeometry(null, pen, path);
        dc.Pop();
    }
}
