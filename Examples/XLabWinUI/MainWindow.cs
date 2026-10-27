using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using XLab.Shared.Services;
using XLab.Shared.ViewModels;
using XLabWinUI.Controls;
using XLabWinUI.Platform;
using Xmax.SDK;
using Xmax.SDK.WinUI;

namespace XLabWinUI;

public sealed partial class MainWindow : Window
{
    internal MainViewModel Model { get; }
    private readonly Grid root = new() { Background = Theme.Brush("#FFFFFF"), RequestedTheme = ElementTheme.Light };
    private readonly ScrollViewer scroller = new() { HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollMode = ScrollMode.Disabled };
    private readonly Canvas page = new() { Background = Theme.Brush("#FFFFFF") };
    private readonly Grid header = new() { Height = 60 };
    private readonly Image brand = new() { Source = new Microsoft.UI.Xaml.Media.Imaging.BitmapImage(new("ms-appx:///Assets/xmax-wordmark.png")), Stretch = Stretch.Uniform };
    private readonly StackPanel homeControls = new() { Orientation = Orientation.Horizontal, Spacing = 12, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
    private readonly StackPanel sessionControls = new() { Orientation = Orientation.Horizontal, Spacing = 12, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
    private readonly StackPanel landing = new();
    private readonly Grid landingStage = new() { HorizontalAlignment = HorizontalAlignment.Center };
    private readonly TextBlock title = Theme.Text("Reimagining Live Video with Xmax", 31.6);
    private readonly TextBlock terms = Theme.Text("", 13, "#9CA3AF");
    private readonly StackPanel workspace = new();
    private readonly Grid localPanel = new() { Background = Theme.Brush("#000000") }, remotePanel = new() { Background = Theme.Brush("#000000") };
    internal readonly XmaxRealtimeVideoView LocalView = new() { ContentMode = VideoContentMode.Fill }, RemoteView = new();
    private readonly TextBlock localPlaceholder = Theme.Text("", 14, "#B3FFFFFF"), resultPlaceholder = Theme.Text("", 14, "#B3FFFFFF");
    private readonly StackPanel localStatistics = new(), remoteStatistics = new();
    private readonly Grid referenceArea = new() { Height = 190 };
    private readonly Dictionary<string, ScrollViewer> referenceScrollers = [];
    internal readonly Dictionary<ReferenceItem, ReferenceTile> ReferenceTiles = [];
    private readonly Dictionary<string, CenteredWrapPanel> referencePanels = [];
    private readonly Dictionary<string, Button> tabs = [];
    private readonly Border freePromptBox = new() { Height = 136, CornerRadius = new(18), Background = Theme.Brush("#F7F8FA"), BorderBrush = Theme.Brush("#E5E7EB"), BorderThickness = new(1), VerticalAlignment = VerticalAlignment.Top, HorizontalAlignment = HorizontalAlignment.Center };
    private readonly TextBox promptEditor = new() { PlaceholderText = "Describe what you want to see…", AcceptsReturn = true, TextWrapping = TextWrapping.Wrap, BorderThickness = new(0), Background = Theme.Brush("#00FFFFFF"), Padding = new(0), FontFamily = Theme.Font, FontSize = 14 };
    private readonly Grid freeReferenceHost = new() { Width = 44, Height = 44, Margin = new(0, 0, 10, 0) };
    private readonly Image freeReferenceImage = new() { Stretch = Stretch.UniformToFill };
    private readonly TextBlock freeReferenceStatus = Theme.Text("", 8, "#FFFFFF");
    private readonly Grid footer = new() { Height = 36 };
    private readonly TextBlock previewFooter = Theme.Text("UI preview · no camera, network session or generated output", 11, "#9CA3AF");
    private readonly Pill previewBadge = new() { Text = "UI Preview · camera off", Accent = true, Margin = new(16, 0, 0, 0), IsHitTestVisible = false };
    internal readonly Pill StartButton = new() { Text = "Start Camera", Glyph = "camera", Primary = true, Height = 52, FontSize = 16, HorizontalAlignment = HorizontalAlignment.Center };
    private readonly Pill cameraButton = new() { Text = "Select Camera", Glyph = "camera", MaxWidth = 218, FontSize = 12 };
    private readonly Pill modelButton = new() { Text = "X2.1", Glyph = "chevron", TrailingGlyph = true, Padding = new(18, 0, 12, 0), ContentSpacing = 6 };
    private readonly Pill sessionModelLabel = new() { Passive = true, IsHitTestVisible = false };
    private readonly Pill keyButton = new() { Glyph = "key", LabelVerticalOffset = 2.75 };
    private readonly Pill statisticsButton = new() { Glyph = "statistics", Secondary = true };
    private readonly Pill volumeButton = new() { Glyph = "muted", Secondary = true };
    private readonly Pill elapsed = new() { Passive = true, MinWidth = 72, IsHitTestVisible = false };
    private readonly Pill recordButton = new() { Text = "Record", Glyph = "record", Secondary = true, IconSize = 12, ContentSpacing = 7, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
    private readonly Pill stopButton = new() { Text = "Stop Session", Glyph = "stop", Destructive = true };
    private readonly Pill freeUpload = new() { Glyph = "plus", Width = 44, Height = 44, Padding = new(0), Margin = new(0, 0, 10, 0) };
    private readonly Pill submit = new() { Glyph = "arrow", Primary = true, Width = 44, Height = 44, Padding = new(0) };
    private readonly Border toast = new() { MaxWidth = 600, CornerRadius = new(14), BorderBrush = Theme.Brush("#14111318"), BorderThickness = new(1), Background = Theme.Brush("#F5FFFFFF"), Padding = new(14), VerticalAlignment = VerticalAlignment.Top, HorizontalAlignment = HorizontalAlignment.Center, Margin = new(28, 90, 28, 0) };
    private readonly TextBlock errorText = Theme.Text("", 14);
    private readonly Microsoft.UI.Dispatching.DispatcherQueueTimer toastTimer;
    private readonly ApiKeyStore? apiKeyStore;
    private bool initialized, previousActive, closing, closed, settingApiKey, layoutPending;

    internal MainWindow(bool preview = false, bool previewSession = false, ApiKeyStore? store = null, bool environmentKey = true)
    {
        Title = preview ? "XLab - WinUI — UI Preview (no live session)" : "XLab - WinUI";
        apiKeyStore = store;
        Model = new(new WinUiDispatcher(DispatcherQueue), new WinUiThumbnailLoader());
        if (preview) Model.ConfigurePreview(previewSession);
        root.DataContext = Model;
        BuildUi();
        Content = root;
        toastTimer = DispatcherQueue.CreateTimer(); toastTimer.Interval = TimeSpan.FromSeconds(6);
        toastTimer.Tick += (_, _) => { toastTimer.Stop(); Model.ClearError(); };
        Model.PropertyChanged += ModelChanged;
        Model.ApiKeyRequired += OpenKey;
        Model.CameraRequired += OpenCameras;
        LocalView.PlaybackFailed += Model.ReportError; RemoteView.PlaybackFailed += Model.ReportError;
        Theme.Bind(LocalView, XmaxRealtimeVideoView.VideoSourceProperty, nameof(Model.LocalVideo), Model);
        Theme.Bind(RemoteView, XmaxRealtimeVideoView.VideoSourceProperty, nameof(Model.RemoteVideo), Model);
        root.SizeChanged += (_, _) => RequestLayout();
        scroller.ViewChanged += (_, _) => RequestLayout();
        root.Loaded += async (_, _) =>
        {
            if (initialized) return;
            initialized = true;
            LoadApiKey(!preview && environmentKey ? Environment.GetEnvironmentVariable("XMAX_API_KEY") : null);
            await Model.InitializeAsync(); RefreshUi(); ArrangePage();
            PageEntry.Reveal(Model.Active ? workspace : landing);
            if (!Model.Active) PageEntry.Reveal(footer);
            Ready?.Invoke();
        };
        ResizeWindow(1360, 890);
        AppWindow.Closing += async (_, args) =>
        {
            if (closed) return;
            args.Cancel = true;
            if (closing) return;
            closing = true; ClosePopups(); toastTimer.Stop(); RefreshUi();
            try
            {
                await Model.StopAsync();
                await PresentRecordingSaveAsync();
                await Model.DisposeAsync();
                closed = true; DispatcherQueue.TryEnqueue(() => Close());
            }
            catch (Exception error) { closing = false; Model.ReportError(error); RefreshUi(); }
        };
        RefreshUi();
    }
    internal event Action? Ready;
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr window);
    internal void ResizeWindow(double width, double height)
    {
        double scale = GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this)) / 96d;
        if (AppWindow.Presenter is Microsoft.UI.Windowing.OverlappedPresenter presenter)
        { presenter.PreferredMinimumWidth = (int)(1180 * scale); presenter.PreferredMinimumHeight = (int)(800 * scale); }
        AppWindow.Resize(new((int)Math.Round(width * scale), (int)Math.Round(height * scale)));
        var area = Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(AppWindow.Id, Microsoft.UI.Windowing.DisplayAreaFallback.Nearest).WorkArea;
        AppWindow.Move(new(area.X + (area.Width - AppWindow.Size.Width) / 2, area.Y + (area.Height - AppWindow.Size.Height) / 2));
    }
    private void ModelChanged(object? sender, PropertyChangedEventArgs args)
    {
        RefreshUi();
        if (Model.HasRecordingToSave && !Model.IsFinishingRecording) _ = PresentRecordingSaveAsync();
        if (args.PropertyName == nameof(Model.Error)) { toastTimer.Stop(); if (Model.Error.Length > 0) toastTimer.Start(); }
    }
    private void RefreshUi()
    {
        bool changed = previousActive != Model.Active;
        homeControls.Visibility = landing.Visibility = footer.Visibility = Model.Active ? Visibility.Collapsed : Visibility.Visible;
        sessionControls.Visibility = workspace.Visibility = Model.Active ? Visibility.Visible : Visibility.Collapsed;
        previewBadge.Visibility = previewFooter.Visibility = Model.IsPreview ? Visibility.Visible : Visibility.Collapsed;
        cameraButton.Visibility = Model.IsPreview ? Visibility.Collapsed : Visibility.Visible;
        cameraButton.Text = Model.SelectedCamera?.Name ?? "Select Camera";
        modelButton.Text = sessionModelLabel.Text = Model.ModelDisplayName;
        keyButton.Text = Model.KeyLabel;
        cameraButton.IsEnabled = modelButton.IsEnabled = keyButton.IsEnabled = StartButton.IsEnabled = Model.CanEdit && !closing && !Model.HasRecordingToSave && !Model.IsFinishingRecording;
        recordButton.Text = Model.RecordLabel;
        recordButton.Glyph = "record";
        recordButton.Accent = Model.IsRecording;
        recordButton.IsLoading = Model.IsFinishingRecording;
        recordButton.IsEnabled = !closing && (Model.CanRecord || Model.IsRecording);
        recordButton.Refresh();
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(recordButton, Model.RecordLabel);
        stopButton.IsEnabled = !closing;
        StartButton.Text = Model.StartLabel; stopButton.Text = Model.StopLabel;
        statisticsButton.Accent = Model.StatisticsVisible; volumeButton.Glyph = Model.VolumeGlyph; elapsed.Text = Model.Elapsed;
        localPlaceholder.Text = Model.LocalPlaceholder; resultPlaceholder.Text = Model.ResultPlaceholder;
        localStatistics.Visibility = remoteStatistics.Visibility = Model.StatisticsVisible ? Visibility.Visible : Visibility.Collapsed;
        localPlaceholder.Margin = resultPlaceholder.Margin = Model.StatisticsVisible ? new(280, 0, 24, 0) : new(24, 0, 24, 0);
        foreach (var (mode, viewer) in referenceScrollers) viewer.Visibility = mode == Model.SelectedMode ? Visibility.Visible : Visibility.Collapsed;
        foreach (var (mode, tab) in tabs)
        {
            bool selected = mode == Model.SelectedMode;
            tab.Background = Theme.Brush(selected ? "#FFFFFF" : "#00FFFFFF"); tab.BorderBrush = Theme.Brush(selected ? "#0D000000" : "#00FFFFFF");
            ((TextBlock)tab.Content).Foreground = Theme.Brush(selected ? "#111318" : "#6B7380");
        }
        freePromptBox.Visibility = Model.SelectedMode == "free" ? Visibility.Visible : Visibility.Collapsed;
        freeUpload.Visibility = Model.HasFreeReference ? Visibility.Collapsed : Visibility.Visible;
        freeReferenceHost.Visibility = Model.HasFreeReference ? Visibility.Visible : Visibility.Collapsed;
        freeReferenceImage.Source = Model.FreeReference?.Thumbnail as ImageSource;
        freeReferenceStatus.Text = Model.FreeReference?.IsUploading == true ? "Uploading…" : Model.FreeReference?.HasError == true ? "Retry" : "";
        submit.IsEnabled = Model.SubmitPromptCommand.CanExecute(null);
        errorText.Text = Model.Error; toast.Visibility = Model.Error.Length == 0 ? Visibility.Collapsed : Visibility.Visible;
        foreach (var tile in ReferenceTiles.Values) tile.IsEnabled = Model.CanEdit && !tile.Item.IsUploading;
        if (changed)
        {
            previousActive = Model.Active; ClosePopups(); scroller.ChangeView(null, 0, null, true);
            if (root.IsLoaded)
            { ArrangePage(); PageEntry.Reveal(Model.Active ? workspace : landing); if (!Model.Active) PageEntry.Reveal(footer); }
        }
        RequestLayout();
    }
    private void RequestLayout()
    {
        if (layoutPending) return;
        layoutPending = true;
        DispatcherQueue.TryEnqueue(() => { layoutPending = false; ArrangePage(); });
    }
    internal void ArrangePage()
    {
        double width = scroller.ViewportWidth > 0 ? scroller.ViewportWidth : root.ActualWidth;
        double height = scroller.ViewportHeight > 0 ? scroller.ViewportHeight : root.ActualHeight;
        if (width <= 0 || height <= 0) return;
        page.Width = width; header.Width = footer.Width = workspace.Width = width - 56; landing.Width = width;
        brand.Height = Math.Clamp(width * .008 + 24, 34, 52); brand.Width = brand.Height * 552 / 172;
        title.FontSize = Math.Clamp(width * .01 + 18, 27, 45);
        landingStage.Margin = new(0, Math.Clamp(width * .011 + 17, 26, 40), 0, 0);
        landingStage.Width = Math.Min(width - 56, Math.Min(940, Math.Max(580, width * .45)));
        double videoAspect = Model.SelectedFormat.Width > 0 ? (double)Model.SelectedFormat.Height / Model.SelectedFormat.Width : 1080d / 1920;
        landingStage.Height = landingStage.Width * videoAspect;
        terms.Width = Math.Max(0, landingStage.Width - 40); terms.FontSize = Math.Clamp(width * .0028 + 9.5, 12, 15);
        StartButton.FontSize = Math.Clamp(width * .003 + 12, 15, 18);
        double padding = Math.Clamp(width * .0075 + 18, 25, 40); StartButton.Padding = new(padding, 0, padding, 0);
        StartButton.Height = Math.Max(20, Math.Ceiling(StartButton.FontSize * 1.3)) + Math.Clamp(width * .0028 + 9, 12, 16) * 2; StartButton.Refresh();
        localPanel.Height = remotePanel.Height = Math.Max(0, (width - 80) / 2 * videoAspect);
        freePromptBox.Width = Math.Min(880, workspace.Width);
        double workspaceHeight = localPanel.Height + 34 + 42 + 28 + 190, workspaceTop = 24 + 60 + 8;
        page.Height = Math.Max(height, Model.Active ? workspaceTop + workspaceHeight + 32 : 760);
        Canvas.SetLeft(header, 28); Canvas.SetTop(header, 24);
        Canvas.SetLeft(workspace, 28); Canvas.SetTop(workspace, workspaceTop);
        landing.Measure(new Size(width, double.PositiveInfinity));
        Canvas.SetTop(landing, Math.Max(24 + 108, (page.Height - landing.DesiredSize.Height) / 2 - 5));
        Canvas.SetLeft(footer, 28); Canvas.SetTop(footer, page.Height - 72);
        previewFooter.Width = width; Canvas.SetTop(previewFooter, page.Height - 20);
    }
    private void OpenLink(string url)
    { try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch (Exception error) { Model.ReportError(error); } }
}
