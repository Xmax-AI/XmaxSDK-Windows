using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace XLabWinUI.Controls;

internal static class Theme
{
    internal static readonly FontFamily Font = new("ms-appx:///Assets/DMSans.ttf#DM Sans");
    internal static SolidColorBrush Brush(string hex)
    {
        string text = hex.TrimStart('#');
        uint value = Convert.ToUInt32(text, 16);
        return new(Color.FromArgb(text.Length == 8 ? (byte)(value >> 24) : (byte)255, (byte)(value >> 16), (byte)(value >> 8), (byte)value));
    }
    internal static readonly ControlTemplate ButtonTemplate = (ControlTemplate)XamlReader.Load("""
        <ControlTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="Button">
          <Border Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="{TemplateBinding CornerRadius}" Padding="{TemplateBinding Padding}">
            <ContentPresenter Content="{TemplateBinding Content}" HorizontalContentAlignment="{TemplateBinding HorizontalContentAlignment}" VerticalContentAlignment="{TemplateBinding VerticalContentAlignment}"/>
          </Border>
        </ControlTemplate>
        """);
    internal static TextBlock Text(string text = "", double size = 13, string color = "#111318") => new()
    { Text = text, FontFamily = Font, FontSize = size, Foreground = Brush(color), VerticalAlignment = VerticalAlignment.Center };
    internal static void Bind(DependencyObject target, DependencyProperty property, string path, object? source = null, BindingMode mode = BindingMode.OneWay)
    {
        var binding = new Binding { Path = new PropertyPath(path), Mode = mode };
        if (source is not null) binding.Source = source;
        BindingOperations.SetBinding(target, property, binding);
    }
    internal static Button Link(string text, Action action)
    {
        var button = new Button { Template = ButtonTemplate, Content = Text(text, 13, "#6B7380"), Padding = new(0), Background = Brush("#00FFFFFF"), BorderThickness = new(0), Height = 36 };
        button.Click += (_, _) => action();
        return button;
    }
}
