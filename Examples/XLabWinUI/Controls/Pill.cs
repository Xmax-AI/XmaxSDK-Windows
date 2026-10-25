using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace XLabWinUI.Controls;

internal sealed class Pill : Button
{
    public static readonly DependencyProperty TextProperty = DependencyProperty.Register(nameof(Text), typeof(string), typeof(Pill), new PropertyMetadata("", Changed));
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(nameof(Glyph), typeof(string), typeof(Pill), new PropertyMetadata("", Changed));
    public static readonly DependencyProperty AccentProperty = DependencyProperty.Register(nameof(Accent), typeof(bool), typeof(Pill), new PropertyMetadata(false, Changed));
    public static readonly DependencyProperty LabelVerticalOffsetProperty = DependencyProperty.Register(nameof(LabelVerticalOffset), typeof(double), typeof(Pill), new PropertyMetadata(0d, Changed));
    public static readonly DependencyProperty ContentSpacingProperty = DependencyProperty.Register(nameof(ContentSpacing), typeof(double), typeof(Pill), new PropertyMetadata(8d, Changed));
    private bool hot, pressed, focused;
    private readonly StackPanel body = new() { Orientation = Orientation.Horizontal, Spacing = 8 };
    private readonly Glyph icon = new() { Width = 16, Height = 16, VerticalAlignment = VerticalAlignment.Center };
    private readonly TextBlock label = Theme.Text();
    private readonly ProgressRing progress = new() { Width = 14, Height = 14, MinWidth = 14, MinHeight = 14, Visibility = Visibility.Collapsed };
    internal bool IsLoading { get; set; }
    internal double? IconSize { get; set; }
    private readonly Glyph chevron = new() { Kind = "chevron", Width = 12, Height = 12, VerticalAlignment = VerticalAlignment.Center, Visibility = Visibility.Collapsed };
    public string Text { get => (string)GetValue(TextProperty); set => SetValue(TextProperty, value); }
    public string Glyph { get => (string)GetValue(GlyphProperty); set => SetValue(GlyphProperty, value); }
    public bool Accent { get => (bool)GetValue(AccentProperty); set => SetValue(AccentProperty, value); }
    public double LabelVerticalOffset { get => (double)GetValue(LabelVerticalOffsetProperty); set => SetValue(LabelVerticalOffsetProperty, value); }
    public double ContentSpacing { get => (double)GetValue(ContentSpacingProperty); set => SetValue(ContentSpacingProperty, value); }
    internal bool Primary { get; set; }
    internal bool Destructive { get; set; }
    internal bool Secondary { get; set; }
    internal bool Passive { get; set; }
    internal bool TrailingGlyph { get; set; }
    internal bool ExtraChevron { get; set; }
    public Pill()
    {
        Template = Theme.ButtonTemplate;
        UseSystemFocusVisuals = false;
        FontFamily = Theme.Font; FontSize = 13; Height = 36; MinWidth = 36; Padding = new(15, 0, 15, 0);
        HorizontalContentAlignment = HorizontalAlignment.Center; VerticalContentAlignment = VerticalAlignment.Center;
        body.Children.Add(icon); body.Children.Add(progress); body.Children.Add(label); body.Children.Add(chevron); Content = body;
        ProtectedCursor = Microsoft.UI.Input.InputSystemCursor.Create(Microsoft.UI.Input.InputSystemCursorShape.Hand);
        PointerEntered += (_, _) => { hot = true; Refresh(); };
        PointerExited += (_, _) => { hot = false; pressed = false; Refresh(); };
        PointerPressed += (_, _) => { pressed = true; focused = false; Refresh(); };
        PointerReleased += (_, _) => { pressed = false; Refresh(); };
        GotFocus += (_, _) => { focused = FocusState == Microsoft.UI.Xaml.FocusState.Keyboard; Refresh(); };
        LostFocus += (_, _) => { focused = false; Refresh(); };
        RegisterPropertyChangedCallback(IsEnabledProperty, (_, _) => Refresh());
        Loaded += (_, _) => Refresh();
    }
    private static void Changed(DependencyObject sender, DependencyPropertyChangedEventArgs args) => ((Pill)sender).Refresh();
    internal void Refresh()
    {
        string color = Primary ? "#FFFFFF" : Destructive ? "#DC2626" : Accent ? "#2F46E2" : Passive ? "#6B7380" : Secondary && !hot ? "#8B919D" : "#111318";
        Foreground = Theme.Brush(color); label.Foreground = Foreground; icon.Foreground = Foreground;
        chevron.Foreground = Foreground; chevron.Visibility = ExtraChevron ? Visibility.Visible : Visibility.Collapsed;
        icon.Kind = Glyph; icon.Visibility = Glyph.Length == 0 || IsLoading ? Visibility.Collapsed : Visibility.Visible;
        progress.Visibility = IsLoading ? Visibility.Visible : Visibility.Collapsed; progress.IsActive = IsLoading; progress.Foreground = Foreground;
        label.Text = Text; label.Visibility = Text.Length == 0 ? Visibility.Collapsed : Visibility.Visible;
        label.RenderTransform = new TranslateTransform { Y = LabelVerticalOffset };
        label.FontFamily = FontFamily; label.FontSize = FontSize; label.FontWeight = Primary || Destructive ? Microsoft.UI.Text.FontWeights.SemiBold : FontWeight;
        label.TextTrimming = TextTrimming.CharacterEllipsis; label.MaxWidth = ExtraChevron ? 150 : double.PositiveInfinity;
        body.Spacing = Text.Length > 0 && Glyph.Length > 0 ? ContentSpacing : 0;
        if (TrailingGlyph && body.Children[0] == icon) { body.Children.Remove(icon); body.Children.Add(icon); }
        icon.Width = icon.Height = IconSize ?? (Primary ? 20 : Height <= 18 ? 11 : 16);
        BorderThickness = new(1); CornerRadius = new(Height / 2);
        BorderBrush = Theme.Brush(Primary ? "#00FFFFFF" : Destructive ? "#F1BABA" : Accent ? "#D7DDFB" : focused ? "#2F46E2" : hot ? "#C9CED6" : "#E2E5EA");
        Background = Primary || Accent || Destructive ? Theme.Brush(Primary ? (focused ? "#2438C4" : "#2F46E2") : Accent ? "#EEF1FF" : "#FDF2F2") : new LinearGradientBrush
        { StartPoint = new(0, 0), EndPoint = new(0, 1), GradientStops = { new() { Color = Microsoft.UI.Colors.White }, new() { Color = Theme.Brush("#F3F4F6").Color, Offset = 1 } } };
        Opacity = !IsEnabled ? .5 : pressed ? .85 : 1;
        if (Text.Length > 0 && string.IsNullOrEmpty(AutomationProperties.GetName(this))) AutomationProperties.SetName(this, Text);
    }
}
