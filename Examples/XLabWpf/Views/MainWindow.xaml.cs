using XLab.Shared.Services;
using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Navigation;
using System.Windows.Threading;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using XLabWpf.Controls;
using XLabWpf.Services;
using XLab.Shared.ViewModels;
using Xmax.SDK.Wpf;

namespace XLabWpf.Views;

public partial class MainWindow : Window
{
    internal MainViewModel Model { get; }
    internal XmaxRealtimeVideoView LocalVideoView => LocalView;
    internal XmaxRealtimeVideoView RemoteVideoView => RemoteView;
    internal bool KeyPopupOpen => KeyPopup.IsOpen;
    internal object VideoDiagnostics => new
    {
        camera = Model.SelectedCamera?.Name,
        local = new { LocalView.IsLoaded, LocalView.IsVisible, LocalView.ActualWidth, LocalView.ActualHeight, LocalView.PresentedFrameCount, clip = LocalPanel.Clip?.Bounds.ToString() },
        remote = new { RemoteView.IsLoaded, RemoteView.IsVisible, RemoteView.ActualWidth, RemoteView.ActualHeight, RemoteView.PresentedFrameCount, clip = RemotePanel.Clip?.Bounds.ToString() }
    };
    internal void SaveVideoScreenshots(string directory)
    {
        if (LocalView.ActualWidth > 0 && LocalView.ActualHeight > 0) SaveElementScreenshot(LocalView, System.IO.Path.Combine(directory, "local-video.png"));
        if (RemoteView.ActualWidth > 0 && RemoteView.ActualHeight > 0) SaveElementScreenshot(RemoteView, System.IO.Path.Combine(directory, "remote-video.png"));
    }
    internal void DismissPopups()
    {
        foreach (var popup in new[] { CameraPopup, ModelPopup, KeyPopup, VolumePopup, WechatPopup }) popup.IsOpen = false;
    }
    internal (double Width, double PageWidth, double HeaderWidth, double StageWidth, double StageHeight) LayoutMetrics =>
        (Root.ActualWidth, Page.ActualWidth, Header.ActualWidth, LandingStage.ActualWidth, LandingStage.ActualHeight);
    internal void SaveUiScreenshot(string path) => SaveElementScreenshot(Root, path);
    internal void SaveKeyPopupScreenshot(string path) => SaveElementScreenshot((FrameworkElement)KeyPopup.Child, path);
    internal async Task VerifyMenusAsync(string? output)
    {
        var original = Model.SelectedCamera;
        string originalKey = Model.ApiKey;
        string? remembered = CameraSelectionStore.Load();
        try
        {
            SetApiKey("ui-smoke-key");
            KeyPopup.IsOpen = true;
            await Task.Delay(180);
            if (ApiKeyInput.Text != Model.ApiKey || KeyButton.Text != new string('*', 16))
                throw new InvalidOperationException("The key editor must show plain text while the header stays masked.");
            ApiKeyInput.AppendText("x");
            if (Model.ApiKey != "ui-smoke-keyx" || KeyButton.Text != new string('*', 16))
                throw new InvalidOperationException("Editing the key did not preserve the header mask.");
            await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
            if (!string.IsNullOrEmpty(output))
            {
                SaveKeyPopupScreenshot(System.IO.Path.Combine(output, "api-key-plain.png"));
                SaveUiScreenshot(System.IO.Path.Combine(output, "api-key-header.png"));
            }
            ApiKeyInput.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(ApiKeyInput)!, 0, Key.Enter) { RoutedEvent = Keyboard.PreviewKeyDownEvent });
            if (KeyPopup.IsOpen) throw new InvalidOperationException("Enter did not close the API Key editor.");
            CameraPopup.IsOpen = true;
            await Task.Delay(180);
            if (!string.IsNullOrEmpty(output)) SaveElementScreenshot((FrameworkElement)CameraPopup.Child, System.IO.Path.Combine(output, "camera-menu.png"));
            if (CameraList.Items.Count > 0)
            {
                CameraList.SelectedIndex = (CameraList.SelectedIndex + 1) % CameraList.Items.Count;
                var selected = CameraList.SelectedItem;
                CameraList.RaiseEvent(new KeyEventArgs(Keyboard.PrimaryDevice, PresentationSource.FromVisual(CameraList)!, 0, Key.Enter) { RoutedEvent = Keyboard.PreviewKeyDownEvent });
                if (CameraPopup.IsOpen || !Equals(Model.SelectedCamera, selected)) throw new InvalidOperationException("Camera selection did not update and close the menu.");
                CameraPopup.IsOpen = true;
                await Task.Delay(100);
                if (CameraList.ItemContainerGenerator.ContainerFromItem(selected!) is ListBoxItem option)
                {
                    option.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice, 0, MouseButton.Left) { RoutedEvent = Mouse.PreviewMouseUpEvent });
                    if (CameraPopup.IsOpen) throw new InvalidOperationException("Clicking the current camera did not close the menu.");
                }
            }
            CameraPopup.IsOpen = false;
            ModelPopup.IsOpen = true;
            await Task.Delay(180);
            if (!string.IsNullOrEmpty(output)) SaveElementScreenshot((FrameworkElement)ModelPopup.Child, System.IO.Path.Combine(output, "model-menu.png"));
            ModelSelected(this, new RoutedEventArgs());
            if (Model.Session is not null) throw new InvalidOperationException("Opening the device menu started a camera session.");
            if (CameraList.Items.Count > 1)
            {
                Model.ApiKey = "ui-camera-selection-test";
                Model.SelectedCamera = null;
                await Model.StartLandingAsync();
                if (!CameraPopup.IsOpen || Model.Session is not null || Model.Active) throw new InvalidOperationException("Starting without an explicit camera choice did not open the device picker.");
            }
        }
        finally
        {
            DismissPopups();
            Model.SelectedCamera = original;
            SetApiKey(originalKey);
            Model.ClearError();
            CameraSelectionStore.Save(remembered);
        }
    }
    internal static (int Minimum, int Maximum) VideoLumaRange(XmaxRealtimeVideoView view)
    {
        if (!view.IsVisible || view.ActualWidth <= 0 || view.ActualHeight <= 0) throw new InvalidOperationException("The video view is not visible.");
        int width = (int)view.ActualWidth, height = (int)view.ActualHeight;
        var image = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
        image.Render(view);
        byte[] pixels = new byte[width * height * 4];
        image.CopyPixels(pixels, width * 4, 0);
        int minimum = 255, maximum = 0;
        for (int y = 0; y < height; y += 8)
            for (int x = 0; x < width; x += 8)
            {
                int offset = (y * width + x) * 4;
                int luma = (pixels[offset + 2] * 77 + pixels[offset + 1] * 150 + pixels[offset] * 29) >> 8;
                minimum = Math.Min(minimum, luma); maximum = Math.Max(maximum, luma);
            }
        return (minimum, maximum);
    }
    private static void SaveElementScreenshot(FrameworkElement element, string path)
    {
        System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);
        element.UpdateLayout();
        var bitmap = new RenderTargetBitmap((int)element.ActualWidth, (int)element.ActualHeight, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(element);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var file = System.IO.File.Create(path); encoder.Save(file);
    }
    private bool closed;
    private bool closing;
    private bool layoutPending;
    private bool settingApiKey;
    private readonly ApiKeyStore? apiKeyStore;
    private readonly DispatcherTimer toastTimer = new() { Interval = TimeSpan.FromSeconds(6) };

    public MainWindow() : this(new ApiKeyStore()) { }
    internal MainWindow(ApiKeyStore? apiKeyStore)
    {
        this.apiKeyStore = apiKeyStore;
        InitializeComponent();
        Model = new(new WpfUiDispatcher(Dispatcher), new WpfThumbnailLoader());
        DataContext = Model;
        LocalView.PlaybackFailed += Model.ReportError;
        RemoteView.PlaybackFailed += Model.ReportError;
        Model.ApiKeyRequired += () => { KeyPopup.IsOpen = true; };
        Model.CameraRequired += () => { CameraPopup.IsOpen = true; };
        Model.PropertyChanged += ModelChanged;
        foreach (var popup in new[] { CameraPopup, ModelPopup, KeyPopup, VolumePopup })
        {
            popup.Placement = PlacementMode.Custom;
            popup.CustomPopupPlacementCallback = (size, target, _) =>
                [new(new Point(target.Width - size.Width, target.Height + 8), PopupPrimaryAxis.Horizontal)];
        }
        toastTimer.Tick += (_, _) => { toastTimer.Stop(); Model.ClearError(); };
        Loaded += (_, _) => ArrangePage();
    }
    internal void LoadApiKey(string? overrideKey = null)
    {
        try { SetApiKey(!string.IsNullOrWhiteSpace(overrideKey) ? overrideKey : apiKeyStore?.Load() ?? ""); }
        catch (Win32Exception error) { Model.ReportError(error); }
    }
    // Programmatic startup/test overrides must never replace the user's remembered key.
    internal void SetApiKey(string key)
    {
        settingApiKey = true;
        try { ApiKeyInput.Text = key; }
        finally { settingApiKey = false; }
    }
    private void ApiKeyChanged(object sender, TextChangedEventArgs e)
    {
        if (Model is null) return;
        Model.ApiKey = ApiKeyInput.Text;
        if (!settingApiKey && !Model.IsPreview)
        {
            try { apiKeyStore?.Save(Model.ApiKey); }
            catch (Win32Exception error) { Model.ReportError(error); }
        }
    }
    internal void VerifyApiKeyMemory(string phase)
    {
        const string first = "xmax-memory-test-记忆", updated = "xmax-memory-test-updated";
        void Check(bool passed) { if (!passed) throw new InvalidOperationException("API Key memory verification failed: " + phase); }
        switch (phase)
        {
            case "write":
                Check(Model.ApiKey.Length == 0);
                ApiKeyInput.Text = first; // Exercise the same event as typing/pasting.
                Check(apiKeyStore!.Load() == first && Model.ApiKey == first);
                LoadApiKey("temporary-environment-override");
                Check(apiKeyStore.Load() == first && Model.ApiKey == "temporary-environment-override");
                break;
            case "read":
                Check(Model.ApiKey == first && ApiKeyInput.Text == first && Model.KeyLabel == new string('*', 16));
                ApiKeyInput.Text = updated;
                Check(apiKeyStore!.Load() == updated);
                break;
            case "clear":
                Check(Model.ApiKey == updated && ApiKeyInput.Text == updated);
                ApiKeyInput.Text = "";
                Check(apiKeyStore!.Load().Length == 0);
                break;
            case "empty": Check(Model.ApiKey.Length == 0 && ApiKeyInput.Text.Length == 0); break;
            default: throw new ArgumentOutOfRangeException(nameof(phase));
        }
    }
    private void ApiKeyKeyDown(object sender, KeyEventArgs e)
    { if (e.Key is Key.Enter or Key.Escape) { KeyPopup.IsOpen = false; e.Handled = true; } }
    private void PageSizeChanged(object sender, SizeChangedEventArgs e) => RequestLayout();
    private void PageScrolled(object sender, ScrollChangedEventArgs e)
    {
        if (e.ViewportWidthChange != 0 || e.ViewportHeightChange != 0) RequestLayout();
    }
    private void RequestLayout()
    {
        if (Model is null || layoutPending) return;
        layoutPending = true;
        // ViewportWidth is updated after SizeChanged; arrange once scrolling/layout has settled.
        Dispatcher.InvokeAsync(() => { layoutPending = false; ArrangePage(); }, DispatcherPriority.Loaded);
    }
    private void ModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(Model.Active))
        { DismissPopups(); Keyboard.ClearFocus(); PageScroller.ScrollToTop(); }
        if (e.PropertyName is nameof(Model.Active) or nameof(Model.IsPreview)) RequestLayout();
        if (e.PropertyName == nameof(Model.Error))
        { toastTimer.Stop(); if (Model.Error.Length > 0) toastTimer.Start(); }
    }
    internal void ArrangePage()
    {
        double width = PageScroller.ViewportWidth > 0 ? PageScroller.ViewportWidth : Root.ActualWidth;
        double height = PageScroller.ViewportHeight > 0 ? PageScroller.ViewportHeight : Root.ActualHeight;
        if (width <= 0 || height <= 0) return;
        Page.Width = width;
        Header.Width = Footer.Width = Workspace.Width = Math.Max(0, width - 56);
        Landing.Width = width;
        // Keep the same visual scale as Qt/macOS XLab; viewport growth must not stretch the hero.
        BrandLogo.Height = Math.Clamp(width * .008 + 24, 34, 52);
        BrandLogo.Width = BrandLogo.Height * 552 / 172;
        LandingTitle.FontSize = Math.Clamp(width * .01 + 18, 27, 45);
        LandingStage.Margin = new Thickness(0, Math.Clamp(width * .011 + 17, 26, 40), 0, 0);
        LandingStage.Width = Math.Min(width - 56, Math.Min(940, Math.Max(580, width * .45)));
        LandingStage.Height = LandingStage.Width * 1024 / 1920;
        LandingTerms.Width = Math.Max(0, LandingStage.Width - 40);
        LandingTerms.FontSize = Math.Clamp(width * .0028 + 9.5, 12, 15);
        StartButton.FontSize = Math.Clamp(width * .003 + 12, 15, 18);
        double buttonPadding = Math.Clamp(width * .0075 + 18, 25, 40);
        StartButton.Padding = new Thickness(buttonPadding, 0, buttonPadding, 0);
        StartButton.Height = Math.Max(20, Math.Ceiling(StartButton.FontSize * 1.3)) + Math.Clamp(width * .0028 + 9, 12, 16) * 2;
        LocalPanel.Height = RemotePanel.Height = Math.Max(0, (width - 80) / 2 * 1024 / 1920);
        FreePromptBox.Width = Math.Min(880, Workspace.Width);
        Workspace.Measure(new Size(Workspace.Width, double.PositiveInfinity));
        double workspaceHeight = LocalPanel.Height + 34 + 42 + 28 + 190;
        const double headerTop = 24;
        double workspaceTop = headerTop + Header.Height + 8;
        double pageHeight = Math.Max(height, Model.Active ? workspaceTop + workspaceHeight + 32 : 760);
        Page.Height = pageHeight;
        Canvas.SetTop(Header, headerTop);
        Canvas.SetTop(Workspace, workspaceTop);
        Landing.Measure(new Size(width, double.PositiveInfinity));
        Canvas.SetTop(Landing, Math.Max(headerTop + 108, (pageHeight - Landing.DesiredSize.Height) / 2 - 5));
        Canvas.SetTop(Footer, pageHeight - 72);
        Canvas.SetTop(PreviewFooter, pageHeight - 20);
        PreviewFooter.Width = width;
    }
    private void CameraClicked(object sender, RoutedEventArgs e) => CameraPopup.IsOpen = !CameraPopup.IsOpen;
    private void ModelClicked(object sender, RoutedEventArgs e) => ModelPopup.IsOpen = !ModelPopup.IsOpen;
    private void KeyClicked(object sender, RoutedEventArgs e) => KeyPopup.IsOpen = !KeyPopup.IsOpen;
    private void VolumeClicked(object sender, RoutedEventArgs e) => VolumePopup.IsOpen = !VolumePopup.IsOpen;
    private void KeyOpened(object sender, EventArgs e) => ApiKeyInput.Focus();
    private void CameraOpened(object sender, EventArgs e)
    {
        CameraList.SelectedItem = Model.SelectedCamera;
        CameraList.Focus();
        if (CameraList.SelectedItem is { } selected) CameraList.ScrollIntoView(selected);
    }
    private void CameraEnabledChanged(object sender, DependencyPropertyChangedEventArgs e)
    { if (e.NewValue is false && CameraPopup is not null) CameraPopup.IsOpen = false; }
    private void CameraOptionClicked(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource is DependencyObject source && ItemsControl.ContainerFromElement(CameraList, source) is ListBoxItem { DataContext: Xmax.SDK.CameraDevice selected })
        { Model.SelectedCamera = selected; CameraPopup.IsOpen = false; e.Handled = true; }
    }
    private void CameraOptionKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key is Key.Enter or Key.Escape)
        {
            if (e.Key == Key.Enter && CameraList.SelectedItem is Xmax.SDK.CameraDevice selected) Model.SelectedCamera = selected;
            CameraPopup.IsOpen = false; e.Handled = true;
        }
    }
    private void ModelSelected(object sender, RoutedEventArgs e) => ModelPopup.IsOpen = false;
    private void StatisticsClicked(object sender, RoutedEventArgs e) => Model.StatisticsVisible = !Model.StatisticsVisible;
    private async void StopClicked(object sender, RoutedEventArgs e)
    { try { await Model.StopAsync(); } catch (Exception error) { Model.ReportError(error); } }
    private async void ReferenceClicked(object sender, RoutedEventArgs e)
    {
        if (sender is not ReferenceTile { DataContext: ReferenceItem item }) return;
        if (item.IsUpload) { ChooseUpload(); return; }
        try
        {
            if (item.LocalPath is not null && item.ReferenceUrl.Length == 0)
                await Model.UploadReferenceAsync(item.LocalPath, item);
            else await Model.SelectReferenceAsync(item);
        }
        catch (Exception error) { Model.ReportError(error); }
    }
    private void UploadClicked(object sender, RoutedEventArgs e) => ChooseUpload();
    private void RemoveFreeReferenceClicked(object sender, RoutedEventArgs e) => Model.RemoveFreeReference();
    private async void FreeReferenceClicked(object sender, RoutedEventArgs e)
    {
        if (Model.FreeReference is not { } item) return;
        try
        {
            if (item.LocalPath is not null && item.ReferenceUrl.Length == 0) await Model.UploadReferenceAsync(item.LocalPath, item);
            else await Model.SelectReferenceAsync(item);
        }
        catch (Exception error) { Model.ReportError(error); }
    }
    private async void ChooseUpload()
    {
        if (Model.IsPreview) { Model.ReportError(new InvalidOperationException("Reference uploads require a live session.")); return; }
        var picker = new OpenFileDialog { Title = "Choose a reference image", Filter = "Images|*.jpg;*.jpeg;*.png;*.webp;*.bmp" };
        if (picker.ShowDialog(this) != true) return;
        try { await Model.UploadReferenceAsync(picker.FileName); } catch (Exception error) { Model.ReportError(error); }
    }
    private void DismissError(object sender, RoutedEventArgs e) => Model.ClearError();
    private void ExternalClicked(object sender, RoutedEventArgs e)
    { if (sender is Button { Tag: string url }) OpenLink(url); }
    private void Navigate(object sender, RequestNavigateEventArgs e) { OpenLink(e.Uri.AbsoluteUri); e.Handled = true; }
    private void OpenLink(string url)
    { try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch (Exception error) { Model.ReportError(error); } }
    private async void WechatClicked(object sender, RoutedEventArgs e)
    {
        WechatPopup.IsOpen = true;
        try { WechatImage.Source = await ThumbnailCache.LoadAsync("https://assets.22duck.fun/image/wechatAccount.jpg"); }
        catch (Exception error) { Model.ReportError(error); }
    }
    protected override async void OnClosing(CancelEventArgs e)
    {
        if (closed) { base.OnClosing(e); return; }
        e.Cancel = true;
        base.OnClosing(e);
        if (closing) return;
        closing = true;
        DismissPopups();
        IsEnabled = false;
        toastTimer.Stop();
        try { await Model.DisposeAsync(); }
        catch (Exception error) { Model.ReportError(error); }
        finally
        {
            closed = true;
            // Disposal can complete synchronously; close after the current Closing event returns.
            if (!Dispatcher.HasShutdownStarted) _ = Dispatcher.InvokeAsync(() => { if (IsVisible) Close(); });
        }
    }
}
