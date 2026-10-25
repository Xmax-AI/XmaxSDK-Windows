using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.Storage.Pickers;
using Windows.System;
using XLab.Shared.ViewModels;
using XLabWinUI.Controls;
using XLabWinUI.Platform;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    private readonly List<Popup> popups = [];
    private TextBox? apiKeyInput;
    private Action<string>? setKeyInput;
    internal Popup? KeyPopup { get; private set; }
    private Popup OpenPopup(FrameworkElement target, FrameworkElement content, double width, double padding = 10)
    {
        ClosePopups();
        var surface = new Border { Width = width, Background = Theme.Brush("#FFFFFF"), BorderBrush = Theme.Brush("#E5E7EB"), BorderThickness = new(1), CornerRadius = new(12), Padding = new(padding), Child = content };
        surface.Measure(new Size(width, double.PositiveInfinity));
        var popup = new Popup { Child = surface, IsLightDismissEnabled = true, XamlRoot = root.XamlRoot };
        var point = target.TransformToVisual(root).TransformPoint(new Point(target.ActualWidth - width, target.ActualHeight + 8));
        popup.HorizontalOffset = Math.Clamp(point.X, 8, Math.Max(8, root.ActualWidth - width - 8)); popup.VerticalOffset = point.Y;
        popups.Add(popup); popup.IsOpen = true;
        return popup;
    }
    private void ClosePopups() { foreach (var popup in popups) popup.IsOpen = false; popups.Clear(); }
    private void OpenCameras()
    {
        var list = new StackPanel { Spacing = 2 };
        foreach (var camera in Model.Cameras)
        {
            bool selected = Model.SelectedCamera?.Id == camera.Id;
            var row = new Grid(); row.ColumnDefinitions.Add(new()); row.ColumnDefinitions.Add(new() { Width = new(22) });
            var text = Theme.Text(camera.Name, 12, selected ? "#2F46E2" : "#111318"); text.FontFamily = new("Microsoft YaHei UI"); text.TextTrimming = TextTrimming.CharacterEllipsis; row.Children.Add(text);
            if (selected) { var check = Theme.Text("✓", 12, "#2F46E2"); check.FontFamily = new("Segoe UI Symbol"); check.HorizontalAlignment = HorizontalAlignment.Right; Grid.SetColumn(check, 1); row.Children.Add(check); }
            var button = new Button { Template = Theme.ButtonTemplate, Content = row, Height = 34, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch, Padding = new(8, 0, 8, 0), CornerRadius = new(8), BorderThickness = new(0), Background = Theme.Brush(selected ? "#EEF1FF" : "#00FFFFFF") };
            AutomationProperties.SetName(button, camera.Name);
            button.Click += (_, _) => { Model.SelectedCamera = camera; ClosePopups(); }; list.Children.Add(button);
        }
        if (Model.Cameras.Count == 0) list.Children.Add(Theme.Text("No cameras found", 12, "#6B7380"));
        OpenPopup(cameraButton, list, 276, 8);
    }
    private void OpenModels()
    {
        var row = new Grid(); row.Children.Add(Theme.Text("X2.1", 13, "#2F46E2"));
        var check = Theme.Text("✓", 13, "#2F46E2"); check.FontFamily = new("Segoe UI Symbol"); check.HorizontalAlignment = HorizontalAlignment.Right; row.Children.Add(check);
        var model = new Button { Template = Theme.ButtonTemplate, Content = row, Height = 36, Background = Theme.Brush("#EEF1FF"), BorderThickness = new(0), CornerRadius = new(8), HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch, Padding = new(10, 0, 10, 0) };
        model.Click += (_, _) => ClosePopups(); OpenPopup(modelButton, model, 240, 7);
    }
    private void OpenKey()
    {
        var stack = new StackPanel { Spacing = 12 };
        apiKeyInput = new TextBox { Text = Model.ApiKey, PlaceholderText = "API Key", IsSpellCheckEnabled = false, IsTextPredictionEnabled = false, FontFamily = Theme.Font, FontSize = 14, Padding = new(12, 9, 12, 9), Background = Theme.Brush("#F7F8FA"), Foreground = Theme.Brush("#111318"), BorderBrush = Theme.Brush("#E5E7EB"), BorderThickness = new(1), CornerRadius = new(10) };
        apiKeyInput.Resources["TextControlBorderThemeThicknessFocused"] = new Thickness(1);
        apiKeyInput.Resources["TextControlBorderBrushFocused"] = Theme.Brush("#2F46E2");
        apiKeyInput.Resources["TextControlBackgroundFocused"] = Theme.Brush("#F7F8FA");
        apiKeyInput.Resources["TextControlPlaceholderForeground"] = Theme.Brush("#9CA3AF");
        var input = apiKeyInput;
        string? ignoredText = Model.ApiKey;
        setKeyInput = value => { ignoredText = value; input.Text = value; };
        AutomationProperties.SetName(apiKeyInput, "API Key input");
        apiKeyInput.TextChanged += (_, _) =>
        {
            string value = input.Text;
            bool programmatic = ignoredText == value;
            ignoredText = null;
            Model.ApiKey = value;
            if (!programmatic && !settingApiKey && !Model.IsPreview)
                try { apiKeyStore?.Save(Model.ApiKey); } catch (Win32Exception error) { Model.ReportError(error); }
        };
        apiKeyInput.KeyDown += (_, args) => { if (args.Key is VirtualKey.Enter or VirtualKey.Escape) { ClosePopups(); args.Handled = true; } };
        stack.Children.Add(apiKeyInput);
        var help = Theme.Text("", 12, "#6B7380"); help.Margin = new(2, 0, 2, 0);
        help.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = "Get an API key from the " }); help.Inlines.Add(LinkInline("Xmax Open Platform", "https://platform.xmaxsi.com/api-keys", "#2F46E2")); help.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = "." }); stack.Children.Add(help);
        KeyPopup = OpenPopup(keyButton, stack, 316, 16);
        DispatcherQueue.TryEnqueue(() => apiKeyInput.Focus(FocusState.Programmatic));
    }
    internal void SetApiKey(string key)
    {
        settingApiKey = true;
        try { Model.ApiKey = key; setKeyInput?.Invoke(key); }
        finally { settingApiKey = false; }
    }
    private void LoadApiKey(string? overrideKey)
    {
        try { SetApiKey(!string.IsNullOrWhiteSpace(overrideKey) ? overrideKey : apiKeyStore?.Load() ?? ""); }
        catch (Win32Exception error) { Model.ReportError(error); }
    }
    private void OpenVolume()
    {
        var stack = new StackPanel { Spacing = 10 };
        var label = Theme.Text($"Remote volume                         {Model.Volume:P0}", 12); stack.Children.Add(label);
        var slider = new Slider { Minimum = 0, Maximum = 1, StepFrequency = .01, Value = Model.Volume };
        slider.ValueChanged += (_, args) => { Model.Volume = args.NewValue; label.Text = $"Remote volume                         {Model.Volume:P0}"; };
        stack.Children.Add(slider); OpenPopup(volumeButton, stack, 240, 14);
    }
    private async Task ChooseUploadAsync()
    {
        if (Model.IsPreview) { Model.ReportError(new InvalidOperationException("Reference uploads require a live session.")); return; }
        try
        {
            var picker = new FileOpenPicker { SuggestedStartLocation = PickerLocationId.PicturesLibrary };
            foreach (string extension in new[] { ".jpg", ".jpeg", ".png", ".webp", ".bmp" }) picker.FileTypeFilter.Add(extension);
            WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(this));
            var file = await picker.PickSingleFileAsync();
            if (file is not null) await Model.UploadReferenceAsync(file.Path);
        }
        catch (Exception error) { Model.ReportError(error); }
    }
    private async Task SelectReferenceAsync(ReferenceItem item)
    {
        try
        {
            if (item.LocalPath is not null && item.ReferenceUrl.Length == 0) await Model.UploadReferenceAsync(item.LocalPath, item);
            else await Model.SelectReferenceAsync(item);
        }
        catch (Exception error) { Model.ReportError(error); }
    }
    private async void OpenWechat()
    {
        try
        {
            var stack = new StackPanel { Spacing = 8 }; stack.Children.Add(new Image { Source = await ThumbnailCache.LoadAsync("https://assets.22duck.fun/image/wechatAccount.jpg"), Width = 180, Height = 180 });
            stack.Children.Add(Theme.Text("Follow us on WeChat", 12)); OpenPopup(brand, stack, 210, 14);
        }
        catch (Exception error) { Model.ReportError(error); }
    }
}
