using System.Windows;
using System.Windows.Controls;

namespace XLabWpf.Controls;

public sealed class Pill : Button
{
    public static readonly DependencyProperty TextProperty = DependencyProperty.Register(nameof(Text), typeof(string), typeof(Pill), new PropertyMetadata(""));
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(nameof(Glyph), typeof(string), typeof(Pill), new PropertyMetadata(""));
    public static readonly DependencyProperty PrimaryProperty = DependencyProperty.Register(nameof(Primary), typeof(bool), typeof(Pill), new PropertyMetadata(false));
    public static readonly DependencyProperty AccentProperty = DependencyProperty.Register(nameof(Accent), typeof(bool), typeof(Pill), new PropertyMetadata(false));
    public static readonly DependencyProperty DestructiveProperty = DependencyProperty.Register(nameof(Destructive), typeof(bool), typeof(Pill), new PropertyMetadata(false));
    public static readonly DependencyProperty TrailingGlyphProperty = DependencyProperty.Register(nameof(TrailingGlyph), typeof(bool), typeof(Pill), new PropertyMetadata(false));
    public static readonly DependencyProperty ShowsShadowProperty = DependencyProperty.Register(nameof(ShowsShadow), typeof(bool), typeof(Pill), new PropertyMetadata(true));
    public static readonly DependencyProperty LabelVerticalOffsetProperty = DependencyProperty.Register(nameof(LabelVerticalOffset), typeof(double), typeof(Pill), new PropertyMetadata(0d));
    public string Text { get => (string)GetValue(TextProperty); set => SetValue(TextProperty, value); }
    public string Glyph { get => (string)GetValue(GlyphProperty); set => SetValue(GlyphProperty, value); }
    public bool Primary { get => (bool)GetValue(PrimaryProperty); set => SetValue(PrimaryProperty, value); }
    public bool Accent { get => (bool)GetValue(AccentProperty); set => SetValue(AccentProperty, value); }
    public bool Destructive { get => (bool)GetValue(DestructiveProperty); set => SetValue(DestructiveProperty, value); }
    public bool TrailingGlyph { get => (bool)GetValue(TrailingGlyphProperty); set => SetValue(TrailingGlyphProperty, value); }
    public bool ShowsShadow { get => (bool)GetValue(ShowsShadowProperty); set => SetValue(ShowsShadowProperty, value); }
    public double LabelVerticalOffset { get => (double)GetValue(LabelVerticalOffsetProperty); set => SetValue(LabelVerticalOffsetProperty, value); }
}
