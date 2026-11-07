//
//  BeautyPanel.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;
using XLab.Shared.ViewModels;

namespace XLabWinUI.Controls;

internal sealed class BeautyPanel : UserControl
{
    internal readonly Slider[] Sliders = new Slider[4];
    internal BeautyPanel()
    {
        Width = 226;
        var rows = new StackPanel { Spacing = 2 };
        string[] names = ["Smoothness", "Lightening", "Sharpness", "Redness"];
        for (int i = 0; i < names.Length; ++i) {
            var row = new Grid { Height = 22, ColumnSpacing = 8 };
            foreach (double width in new[] { 70d, 94d, 30d }) row.ColumnDefinitions.Add(new() { Width = new(width) });
            var title = Theme.Text(names[i], 11, "#E6FFFFFF"); title.VerticalAlignment = VerticalAlignment.Center; row.Children.Add(title);
            var slider = new Slider { Minimum = 0, Maximum = 1, StepFrequency = .01, MinWidth = 0, MinHeight = 0, Height = 22, Padding = new(0), VerticalAlignment = VerticalAlignment.Center, RequestedTheme = ElementTheme.Dark };
            AutomationProperties.SetName(slider, names[i]);
            Sliders[i] = slider; Grid.SetColumn(slider, 1); row.Children.Add(slider);
            var value = Theme.Text("0%", 11, "#E6FFFFFF"); value.TextAlignment = TextAlignment.Right; value.VerticalAlignment = VerticalAlignment.Center;
            Grid.SetColumn(value, 2); row.Children.Add(value);
            slider.ValueChanged += (_, args) => value.Text = Math.Round(args.NewValue * 100) + "%";
            rows.Children.Add(row);
        }
        Content = new Border { CornerRadius = new(8), Padding = new(7), BorderThickness = new(1), BorderBrush = Theme.Brush("#1FFFFFFF"), Background = Theme.Brush("#99000000"), Child = rows };
    }
    internal void Bind(MainViewModel model)
    {
        string[] paths = [nameof(model.BeautySmoothness), nameof(model.BeautyLightening), nameof(model.BeautySharpness), nameof(model.BeautyRedness)];
        for (int i = 0; i < Sliders.Length; ++i) Theme.Bind(Sliders[i], RangeBase.ValueProperty, paths[i], model, BindingMode.TwoWay);
    }
}
