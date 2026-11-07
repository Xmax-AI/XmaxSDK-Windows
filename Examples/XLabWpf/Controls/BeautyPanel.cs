//
//  BeautyPanel.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Markup;
using System.Windows.Media;
using XLab.Shared.ViewModels;

namespace XLabWpf.Controls;

public sealed class BeautyPanel : UserControl
{
    internal readonly Slider[] Sliders = new Slider[4];
    public BeautyPanel()
    {
        Width = 226;
        var rows = new StackPanel();
        var foreground = (Brush)new BrushConverter().ConvertFromString("#E6FFFFFF")!;
        string[] names = ["Smoothness", "Lightening", "Sharpness", "Redness"];
        string[] paths = [nameof(MainViewModel.BeautySmoothness), nameof(MainViewModel.BeautyLightening), nameof(MainViewModel.BeautySharpness), nameof(MainViewModel.BeautyRedness)];
        var style = (Style)XamlReader.Parse("""
            <Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Slider">
              <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Slider">
                <Grid Height="22"><Border Height="3" Background="#40FFFFFF" CornerRadius="1.5"/>
                  <Track x:Name="PART_Track" Minimum="{TemplateBinding Minimum}" Maximum="{TemplateBinding Maximum}" Value="{Binding Value, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}">
                    <Track.DecreaseRepeatButton><RepeatButton Command="{x:Static Slider.DecreaseLarge}" Focusable="False"><RepeatButton.Template><ControlTemplate><Border Height="3" CornerRadius="1.5" Background="#9A9FF6"/></ControlTemplate></RepeatButton.Template></RepeatButton></Track.DecreaseRepeatButton>
                    <Track.Thumb><Thumb Width="10" Height="10"><Thumb.Template><ControlTemplate><Border Background="White" CornerRadius="5"/></ControlTemplate></Thumb.Template></Thumb></Track.Thumb>
                    <Track.IncreaseRepeatButton><RepeatButton Command="{x:Static Slider.IncreaseLarge}" Focusable="False"><RepeatButton.Template><ControlTemplate><Border Background="Transparent"/></ControlTemplate></RepeatButton.Template></RepeatButton></Track.IncreaseRepeatButton>
                  </Track>
                </Grid>
              </ControlTemplate></Setter.Value></Setter>
            </Style>
            """);
        for (int i = 0; i < names.Length; ++i) {
            var row = new Grid { Height = 22, Margin = new(0, 0, 0, i == 3 ? 0 : 2) };
            foreach (double width in new[] { 70d, 8d, 94d, 8d, 30d }) row.ColumnDefinitions.Add(new() { Width = new(width) });
            row.Children.Add(new TextBlock { Text = names[i], FontSize = 11, Foreground = foreground, VerticalAlignment = VerticalAlignment.Center });
            var slider = new Slider { Minimum = 0, Maximum = 1, TickFrequency = .01, SmallChange = .01, LargeChange = .1, Height = 22, Style = style, IsMoveToPointEnabled = true };
            slider.SetBinding(Slider.ValueProperty, new Binding(paths[i]) { Mode = BindingMode.TwoWay, UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged });
            AutomationProperties.SetName(slider, names[i]); Sliders[i] = slider; Grid.SetColumn(slider, 2); row.Children.Add(slider);
            var value = new TextBlock { FontSize = 11, Foreground = foreground, TextAlignment = TextAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
            value.SetBinding(TextBlock.TextProperty, new Binding(paths[i]) { StringFormat = "{0:P0}" }); Grid.SetColumn(value, 4); row.Children.Add(value);
            rows.Children.Add(row);
        }
        Content = new Border { CornerRadius = new(8), Padding = new(7), BorderThickness = new(1), BorderBrush = (Brush)new BrushConverter().ConvertFromString("#1FFFFFFF")!, Background = (Brush)new BrushConverter().ConvertFromString("#99000000")!, Child = rows };
    }
}
