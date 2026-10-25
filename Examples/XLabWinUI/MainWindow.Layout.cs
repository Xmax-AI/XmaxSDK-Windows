using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using XLab.Shared.ViewModels;
using XLabWinUI.Controls;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    private void BuildUi()
    {
        cameraButton.ExtraChevron = true; cameraButton.Padding = new(11, 0, 11, 0); cameraButton.FontFamily = new("Microsoft YaHei UI");
        scroller.Content = page; root.Children.Add(scroller);
        foreach (var element in new UIElement[] { header, landing, workspace, footer, previewFooter }) page.Children.Add(element);
        previewFooter.TextAlignment = TextAlignment.Center;
        var branding = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        branding.Children.Add(brand); branding.Children.Add(previewBadge); header.Children.Add(branding);
        foreach (var button in new[] { cameraButton, modelButton, keyButton }) homeControls.Children.Add(button);
        header.Children.Add(homeControls); header.Children.Add(sessionControls);
        foreach (var element in new UIElement[] { sessionModelLabel, statisticsButton, volumeButton, elapsed, recordButton, stopButton }) sessionControls.Children.Add(element);
        AutomationProperties.SetName(cameraButton, "Select Camera"); AutomationProperties.SetName(keyButton, "API Key");
        AutomationProperties.SetName(StartButton, "Start Camera"); AutomationProperties.SetName(statisticsButton, "Show / hide statistics"); AutomationProperties.SetName(volumeButton, "Remote volume");
        ToolTipService.SetToolTip(statisticsButton, "Show / hide statistics"); ToolTipService.SetToolTip(volumeButton, "Remote volume"); ToolTipService.SetToolTip(keyButton, "API Key");
        cameraButton.Click += (_, _) => OpenCameras(); modelButton.Click += (_, _) => OpenModels(); keyButton.Click += (_, _) => OpenKey();
        statisticsButton.Click += (_, _) => Model.StatisticsVisible = !Model.StatisticsVisible;
        recordButton.Click += async (_, _) => await ToggleRecordingAsync();
        volumeButton.Click += (_, _) => OpenVolume();
        stopButton.Click += async (_, _) => { try { await Model.StopAsync(); } catch (Exception error) { Model.ReportError(error); } };
        StartButton.Click += (_, _) => Model.StartCommand.Execute(null);
        title.HorizontalAlignment = HorizontalAlignment.Center; landing.Children.Add(title);
        landingStage.Children.Add(new CheckerboardSurface());
        var actions = new StackPanel { HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
        actions.Children.Add(StartButton); actions.Children.Add(MicrophoneToggle()); landingStage.Children.Add(actions);
        terms.TextAlignment = TextAlignment.Center; terms.TextWrapping = TextWrapping.Wrap; terms.HorizontalAlignment = HorizontalAlignment.Center; terms.VerticalAlignment = VerticalAlignment.Bottom; terms.Margin = new(20, 0, 20, 28);
        terms.Inlines.Add(new Run { Text = "By clicking Start, you agree to our " });
        terms.Inlines.Add(LinkInline("Terms of Use", "https://platform.xmaxai.com/docs/resources/terms", "#C3C6CC"));
        terms.Inlines.Add(new Run { Text = " and " }); terms.Inlines.Add(LinkInline("Privacy Policy", "https://platform.xmaxai.com/docs/resources/privacy", "#C3C6CC")); terms.Inlines.Add(new Run { Text = "." });
        landingStage.Children.Add(terms); landing.Children.Add(landingStage);
        var copyright = Theme.Text("Copyright © 2026 XMAX.AI PTE.LTD. All rights reserved.", 13, "#6B7380"); copyright.HorizontalAlignment = HorizontalAlignment.Center; copyright.Margin = new(0, 16, 0, 0); landing.Children.Add(copyright);
        var videos = new Grid(); videos.ColumnDefinitions.Add(new()); videos.ColumnDefinitions.Add(new() { Width = new(24) }); videos.ColumnDefinitions.Add(new());
        ConfigureVideo(localPanel, LocalView, localPlaceholder, localStatistics, false); ConfigureVideo(remotePanel, RemoteView, resultPlaceholder, remoteStatistics, true);
        videos.Children.Add(localPanel); Grid.SetColumn(remotePanel, 2); videos.Children.Add(remotePanel); workspace.Children.Add(videos);
        var tabBar = new Border { HorizontalAlignment = HorizontalAlignment.Center, CornerRadius = new(21), Background = Theme.Brush("#1A787880"), Height = 42, Padding = new(4), Margin = new(0, 34, 0, 28) };
        var tabRow = new StackPanel { Orientation = Orientation.Horizontal };
        foreach (var (mode, text) in new[] { ("charx", "Character Swap"), ("clothx", "Virtual Try-On"), ("vibex", "Style Transfer"), ("free", "Free Prompt") })
        {
            var label = Theme.Text(text, 14, "#6B7380"); label.FontWeight = Microsoft.UI.Text.FontWeights.Medium;
            var tab = new Button { Template = Theme.ButtonTemplate, Content = label, Height = 34, Padding = new(22, 0, 22, 0), CornerRadius = new(17), BorderThickness = new(1) };
            AutomationProperties.SetName(tab, text); tab.Click += (_, _) => Model.SelectedMode = mode; tabs[mode] = tab; tabRow.Children.Add(tab);
        }
        tabBar.Child = tabRow; workspace.Children.Add(tabBar); workspace.Children.Add(referenceArea);
        BuildReferences("charx", Model.CharacterReferences); BuildReferences("clothx", Model.ClothingReferences); BuildReferences("vibex", Model.StyleReferences);
        foreach (var resource in new[] { "TextControlBackground", "TextControlBackgroundPointerOver", "TextControlBackgroundFocused", "TextControlBackgroundDisabled" })
            promptEditor.Resources[resource] = Theme.Brush("#00FFFFFF");
        promptEditor.Resources["TextControlBorderThemeThicknessFocused"] = new Thickness(0);
        var promptContent = new Grid { Margin = new(12) }; promptEditor.Margin = new(0, 0, 112, 0); promptContent.Children.Add(promptEditor);
        promptEditor.TextChanged += (_, _) => Model.Prompt = promptEditor.Text;
        var freeActions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Bottom };
        freeUpload.Click += async (_, _) => await ChooseUploadAsync(); freeActions.Children.Add(freeUpload);
        var freeButton = new Button { Template = Theme.ButtonTemplate, Padding = new(0), BorderThickness = new(0), Background = Theme.Brush("#E5E7EB"), CornerRadius = new(22), Content = freeReferenceImage };
        RoundedClip.Attach(freeButton, 22); freeButton.Click += async (_, _) => { if (Model.FreeReference is { } item) await SelectReferenceAsync(item); };
        freeReferenceStatus.IsHitTestVisible = false;
        freeReferenceHost.Children.Add(freeButton); freeReferenceHost.Children.Add(freeReferenceStatus);
        var remove = new Button { Template = Theme.ButtonTemplate, Width = 18, Height = 18, MinWidth = 18, MinHeight = 18, Padding = new(0), BorderThickness = new(0), CornerRadius = new(9), Background = Theme.Brush("#3D3D3D"), HorizontalContentAlignment = HorizontalAlignment.Center, VerticalContentAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Top, Margin = new(0, -4, -5, 0), Content = new Glyph { Kind = "close", Width = 11, Height = 11, Foreground = Theme.Brush("#FFFFFF") } };
        remove.PointerEntered += (_, _) => remove.Background = Theme.Brush("#525252");
        remove.PointerExited += (_, _) => remove.Background = Theme.Brush("#3D3D3D");
        AutomationProperties.SetName(remove, "Remove reference"); remove.Click += (_, _) => Model.RemoveFreeReference(); freeReferenceHost.Children.Add(remove); freeActions.Children.Add(freeReferenceHost);
        submit.Click += (_, _) => Model.SubmitPromptCommand.Execute(null); ToolTipService.SetToolTip(submit, "Submit prompt"); freeActions.Children.Add(submit);
        promptContent.Children.Add(freeActions); freePromptBox.Child = promptContent; referenceArea.Children.Add(freePromptBox);
        BuildFooter();
        var toastRow = new Grid(); toastRow.ColumnDefinitions.Add(new() { Width = new(18) }); toastRow.ColumnDefinitions.Add(new()); toastRow.ColumnDefinitions.Add(new() { Width = new(24) });
        toastRow.Children.Add(new Glyph { Kind = "warning", Width = 18, Height = 18, Foreground = Theme.Brush("#E5484D") });
        errorText.TextWrapping = TextWrapping.Wrap; errorText.Margin = new(14, 0, 14, 0); Grid.SetColumn(errorText, 1); toastRow.Children.Add(errorText);
        var dismiss = new Pill { Glyph = "close", Width = 24, Height = 24, MinWidth = 24, Padding = new(0) }; dismiss.Click += (_, _) => Model.ClearError(); Grid.SetColumn(dismiss, 2); toastRow.Children.Add(dismiss);
        toast.Child = toastRow; root.Children.Add(toast);
    }
    private Button MicrophoneToggle()
    {
        var button = new Button { Template = Theme.ButtonTemplate, Padding = new(0), Background = Theme.Brush("#47000000"), BorderBrush = Theme.Brush("#29FFFFFF"), BorderThickness = new(1), CornerRadius = new(16), Width = 158, Height = 32, HorizontalAlignment = HorizontalAlignment.Center, Margin = new(0, 20, 0, 0) };
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Center };
        row.Children.Add(new Glyph { Kind = "microphone", Width = 14, Height = 14, Foreground = Theme.Brush("#8CFFFFFF"), VerticalAlignment = VerticalAlignment.Center }); row.Children.Add(Theme.Text("Microphone", 13, "#8CFFFFFF"));
        var track = new Border { Width = 30, Height = 18, CornerRadius = new(9), Background = Theme.Brush("#2EFFFFFF"), VerticalAlignment = VerticalAlignment.Center };
        var knob = new Microsoft.UI.Xaml.Shapes.Ellipse { Width = 14, Height = 14, Fill = Theme.Brush("#FFFFFF"), HorizontalAlignment = HorizontalAlignment.Left, Margin = new(2, 0, 0, 0) };
        track.Child = knob; row.Children.Add(track); button.Content = row;
        AutomationProperties.SetName(button, "Microphone"); Theme.Bind(button, Button.IsEnabledProperty, nameof(Model.CanEdit), Model);
        button.Click += (_, _) => { Model.Microphone = !Model.Microphone; track.Background = Theme.Brush(Model.Microphone ? "#2F46E2" : "#2EFFFFFF"); knob.HorizontalAlignment = Model.Microphone ? HorizontalAlignment.Right : HorizontalAlignment.Left; knob.Margin = Model.Microphone ? new(0, 0, 2, 0) : new(2, 0, 0, 0); };
        return button;
    }
    private void ConfigureVideo(Grid panel, UIElement video, TextBlock placeholder, StackPanel metrics, bool result)
    {
        RoundedClip.Attach(panel, 24); panel.Children.Add(video);
        placeholder.TextAlignment = TextAlignment.Center; placeholder.TextWrapping = TextWrapping.Wrap; placeholder.VerticalAlignment = VerticalAlignment.Center; panel.Children.Add(placeholder);
        metrics.Margin = new(14); metrics.VerticalAlignment = VerticalAlignment.Top; metrics.HorizontalAlignment = HorizontalAlignment.Left;
        foreach (var rows in result ? new[] { Model.DownlinkRows, Model.DownlinkNetworkRows } : new[] { Model.TimingRows, Model.UplinkRows, Model.UplinkNetworkRows }) metrics.Children.Add(MetricBox(rows));
        panel.Children.Add(metrics);
        var titleText = Theme.Text(result ? "Result" : "Local", 12, "#A6FFFFFF"); titleText.TextTrimming = TextTrimming.CharacterEllipsis;
        if (!result) { titleText.FontFamily = new("Microsoft YaHei UI"); Theme.Bind(titleText, TextBlock.TextProperty, nameof(Model.LocalTitle), Model); }
        panel.Children.Add(new Border { Background = Theme.Brush("#59000000"), CornerRadius = new(8), Padding = new(8, 4, 8, 4), Margin = new(14, 0, 14, 12), VerticalAlignment = VerticalAlignment.Bottom, HorizontalAlignment = HorizontalAlignment.Left, Child = titleText });
        if (result) { var watermark = Theme.Text("✦ Xmax", 13, "#CCFFFFFF"); watermark.HorizontalAlignment = HorizontalAlignment.Right; watermark.VerticalAlignment = VerticalAlignment.Top; watermark.Margin = new(16); watermark.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold; panel.Children.Add(watermark); }
    }
    private static Border MetricBox(MetricRow[] rows)
    {
        var stack = new StackPanel();
        foreach (var row in rows)
        {
            var grid = new Grid { Height = 17 }; grid.ColumnDefinitions.Add(new()); grid.ColumnDefinitions.Add(new() { Width = GridLength.Auto });
            grid.Children.Add(Theme.Text(row.Label, 11, "#E6FFFFFF"));
            var value = Theme.Text("—", 11, "#E6FFFFFF"); value.FontFamily = new("Consolas"); Theme.Bind(value, TextBlock.TextProperty, nameof(row.Value), row); Grid.SetColumn(value, 1); grid.Children.Add(value);
            if (row.Label == "Total startup") stack.Children.Add(new Border { BorderThickness = new(0, 1, 0, 0), BorderBrush = Theme.Brush("#30FFFFFF"), Padding = new(0, 6, 0, 0), Margin = new(0, 4, 0, 0), Child = grid });
            else stack.Children.Add(grid);
        }
        return new() { Width = 250, CornerRadius = new(10), Background = Theme.Brush("#99000000"), BorderBrush = Theme.Brush("#1FFFFFFF"), BorderThickness = new(1), Padding = new(10, 8, 10, 8), Margin = new(0, 0, 0, 8), Child = stack };
    }
    private void BuildReferences(string mode, ObservableCollection<ReferenceItem> items)
    {
        var panel = new CenteredWrapPanel(); referencePanels[mode] = panel;
        var viewer = new ScrollViewer { Content = panel, VerticalScrollBarVisibility = ScrollBarVisibility.Hidden, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, HorizontalScrollMode = ScrollMode.Disabled };
        referenceScrollers[mode] = viewer; referenceArea.Children.Add(viewer);
        void Sync()
        {
            foreach (var item in items)
                if (!ReferenceTiles.ContainsKey(item)) { var tile = new ReferenceTile(item); tile.Click += async (_, _) => { if (item.IsUpload) await ChooseUploadAsync(); else await SelectReferenceAsync(item); }; ReferenceTiles[item] = tile; }
            panel.Children.Clear(); foreach (var item in items) panel.Children.Add(ReferenceTiles[item]);
        }
        items.CollectionChanged += (_, _) => Sync(); Sync();
    }
    private void BuildFooter()
    {
        var socials = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 18 };
        foreach (var (glyph, name, url) in new[] { ("wechat", "WeChat", ""), ("socialX", "X", "https://x.com/XmaxAIOfficial"), ("globe", "Website", "https://www.xmaxai.com") })
        {
            var link = Theme.Link("", () => { if (url.Length == 0) OpenWechat(); else OpenLink(url); }); link.Content = new Glyph { Kind = glyph, Width = 20, Height = 20, Foreground = Theme.Brush("#6B7380") }; link.Width = 24; ToolTipService.SetToolTip(link, name); socials.Children.Add(link);
        }
        footer.Children.Add(socials);
        var links = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 24, HorizontalAlignment = HorizontalAlignment.Right };
        foreach (var (text, url) in new[] { ("Contact Us", "mailto:contact@xmax.ai"), ("GitHub", "https://github.com/Xmax-AI/XmaxSDK-Windows"), ("API Platform", "https://platform.xmaxai.com") }) links.Children.Add(Theme.Link(text, () => OpenLink(url)));
        footer.Children.Add(links);
    }
    private static Hyperlink LinkInline(string text, string url, string color)
    { var link = new Hyperlink { NavigateUri = new(url), Foreground = Theme.Brush(color) }; link.Inlines.Add(new Run { Text = text }); return link; }
}
