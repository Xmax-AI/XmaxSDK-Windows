using System.IO;
using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;
using XLab.Shared.ViewModels;
using Xmax.SDK;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    internal async Task SaveScreenshotAsync(string name, FrameworkElement? element = null)
    {
        string? output = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
        if (string.IsNullOrEmpty(output)) return;
        Directory.CreateDirectory(output);
        var target = element ?? root;
        var bitmap = new RenderTargetBitmap(); await bitmap.RenderAsync(target, (int)target.ActualWidth, (int)target.ActualHeight);
        using var reader = DataReader.FromBuffer(await bitmap.GetPixelsAsync());
        byte[] pixels = new byte[reader.UnconsumedBufferLength]; reader.ReadBytes(pixels);
        using var stream = new InMemoryRandomAccessStream();
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, pixels);
        encoder.BitmapTransform.ScaledWidth = (uint)target.ActualWidth;
        encoder.BitmapTransform.ScaledHeight = (uint)target.ActualHeight;
        await encoder.FlushAsync();
        using var encoded = new DataReader(stream.GetInputStreamAt(0)); await encoded.LoadAsync((uint)stream.Size);
        byte[] png = new byte[stream.Size]; encoded.ReadBytes(png); await File.WriteAllBytesAsync(Path.Combine(output, name + ".png"), png);
    }
    private async Task VerifyPromptBackgroundAsync()
    {
        promptEditor.ApplyTemplate();
        foreach (var state in new[] { "Normal", "PointerOver", "Focused" })
        {
            if (state == "Focused") promptEditor.Focus(FocusState.Programmatic);
            Check(VisualStateManager.GoToState(promptEditor, state, false), "Missing prompt input state: " + state);
            await Task.Delay(100);
            var bitmap = new RenderTargetBitmap();
            await bitmap.RenderAsync(freePromptBox, (int)freePromptBox.ActualWidth, (int)freePromptBox.ActualHeight);
            using var reader = DataReader.FromBuffer(await bitmap.GetPixelsAsync());
            byte[] pixels = new byte[reader.UnconsumedBufferLength]; reader.ReadBytes(pixels);
            var point = promptEditor.TransformToVisual(freePromptBox).TransformPoint(new Windows.Foundation.Point(promptEditor.ActualWidth / 2, promptEditor.ActualHeight * .7));
            int x = (int)(point.X * bitmap.PixelWidth / freePromptBox.ActualWidth);
            int y = (int)(point.Y * bitmap.PixelHeight / freePromptBox.ActualHeight);
            int offset = (y * bitmap.PixelWidth + x) * 4;
            Check(Math.Abs(pixels[offset] - 250) <= 2 && Math.Abs(pixels[offset + 1] - 248) <= 2 && Math.Abs(pixels[offset + 2] - 247) <= 2,
                "Prompt input must retain the surrounding gray background in state " + state);
            await SaveScreenshotAsync("prompt-" + state.ToLowerInvariant(), freePromptBox);
        }
        string original = promptEditor.Text;
        promptEditor.Text = "Input appearance check";
        await UntilAsync(() => Model.Prompt == promptEditor.Text);
        promptEditor.Text = original;
    }
    private async Task VerifyReferenceCloseAsync()
    {
        Model.SelectedMode = "free";
        // Seed the visual fixture without uploading or creating a cloud session.
        typeof(MainViewModel).GetProperty(nameof(MainViewModel.FreeReference))!.SetValue(Model,
            new ReferenceItem { Mode = "free", Name = "Reference", Thumbnail = Model.CharacterReferences.First(item => !item.IsUpload).Thumbnail });
        await Task.Delay(150);
        await SaveScreenshotAsync("free-reference-close", freePromptBox);
        var removeReference = freeReferenceHost.Children.OfType<Microsoft.UI.Xaml.Controls.Button>().Single(button => Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(button) == "Remove reference");
        var removePeer = FrameworkElementAutomationPeer.CreatePeerForElement(removeReference)!;
        ((IInvokeProvider)removePeer.GetPattern(PatternInterface.Invoke)).Invoke();
        Check(Model.FreeReference is null, "The reference close button did not remove the custom image.");
    }
    private static async Task UntilAsync(Func<bool> condition, int seconds = 20)
    {
        DateTime deadline = DateTime.UtcNow.AddSeconds(seconds);
        while (!condition()) { if (DateTime.UtcNow >= deadline) throw new TimeoutException("WinUI verification did not reach the expected state."); await Task.Delay(30); }
    }
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private async Task VerifyModelsAsync()
    {
        string original = Model.SelectedModel;
        try
        {
            foreach (string identifier in MainViewModel.Models.Reverse())
            {
                OpenModels();
                var surface = (Border)popups.Single().Child;
                var list = (StackPanel)surface.Child;
                Check(list.Children.Count == 2, "The model menu must include domestic and overseas models.");
                var option = list.Children.OfType<Button>().Single(button => Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(button) == identifier);
                await UntilAsync(() => option.IsLoaded);
                if (identifier.EndsWith("-us")) await SaveScreenshotAsync("model-menu", surface);
                var peer = FrameworkElementAutomationPeer.CreatePeerForElement(option)!;
                ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
                await UntilAsync(() => Model.SelectedModel == identifier && popups.Count == 0);
                Check(modelButton.Text == Model.ModelDisplayName && sessionModelLabel.Text == Model.ModelDisplayName, "Model labels did not follow selection.");
                Check(Model.Session is null, "Selecting a model started a session.");
            }
        }
        finally { ClosePopups(); Model.SelectedModel = original; }
    }
    private async Task ClickReferenceAsync(ReferenceItem item)
    {
        Model.SelectedMode = item.Mode; root.UpdateLayout();
        var tile = ReferenceTiles[item]; tile.StartBringIntoView();
        var peer = FrameworkElementAutomationPeer.CreatePeerForElement(tile)!;
        ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
        await UntilAsync(() => item.IsSelected);
        await Model.WaitForConditionsAsync().WaitAsync(TimeSpan.FromSeconds(45));
        Check(Model.Error.Length == 0, Model.Error);
    }
    internal async Task RunSmokeAsync(string mode)
    {
        bool passed = false, referenceUpdatePassed = false;
        string? error = null;
        var transitions = new List<object>();
        var pageEntryStates = new List<RealtimeState>();
        void ObservePageEntry(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
        {
            if (args.PropertyName == nameof(Model.Active) && Model.Active && !Model.IsPreview) pageEntryStates.Add(Model.State);
        }
        Model.PropertyChanged += ObservePageEntry;
        try
        {
            await Task.Delay(650);
            if (mode == "--smoke-test-ui")
            {
                await VerifyModelsAsync();
                await SaveScreenshotAsync("aligned-landing");
                foreach (var (width, height, name) in new[] { (1920,1080,"wide"), (1180,800,"minimum") })
                {
                    ResizeWindow(width,height); await Task.Delay(250); ArrangePage();
                    Check(landingStage.Width <= 940 && Math.Abs(landingStage.Width * 1024 / 1920 - landingStage.Height) < .5, "Landing proportions changed.");
                    await SaveScreenshotAsync("landing-" + name);
                }
                ResizeWindow(1360,890); await Task.Delay(200);
                await Model.StartLandingAsync(); transitions.Add(await VerifyTransitionAsync(true));
                await UntilAsync(() => Model.CharacterReferences.All(item => item.IsUpload || item.Thumbnail is not null || item.HasError), 40);
                await ClickReferenceAsync(Model.CharacterReferences.First(item => item.Id == "charx-3"));
                await ClickReferenceAsync(Model.ClothingReferences.First(item => !item.IsUpload));
                await ClickReferenceAsync(Model.StyleReferences.First(item => !item.IsUpload));
                await ClickReferenceAsync(Model.CharacterReferences.First(item => item.Id == "charx-3"));
                await SaveScreenshotAsync("aligned-session");
                Model.StatisticsVisible = false; Model.SelectedMode = "free"; await Task.Delay(100); await SaveScreenshotAsync("aligned-free-prompt");
                await VerifyPromptBackgroundAsync();
                await VerifyReferenceCloseAsync();
                await Model.SubmitPromptAsync(); Check(Model.Session is null, "UI preview created a native session.");
                Model.StatisticsVisible = true; await Model.StopAsync(); transitions.Add(await VerifyTransitionAsync(false));
                await Model.StartLandingAsync(); await Task.Delay(50); await Model.StopAsync(); await Task.Delay(50); await Model.StartLandingAsync();
                transitions.Add(await VerifyTransitionAsync(true)); await Model.StopAsync(); transitions.Add(await VerifyTransitionAsync(false));
            }
            else if (mode == "--smoke-test-recording") await VerifyRecordingAsync();
            else if (mode == "--smoke-test-reference-close")
            {
                await VerifyReferenceCloseAsync();
            }
            else if (mode == "--smoke-test-key-gate")
            {
                SetApiKey(""); await Model.StartLandingAsync(); await Task.Delay(200);
                Check(!Model.Active && KeyPopup?.IsOpen == true, "Missing Key did not open the input popup.");
                await SaveScreenshotAsync("api-key-popup", (FrameworkElement)KeyPopup!.Child);
            }
            else if (mode == "--smoke-test-frames") await VerifyFramesAsync();
            else if (mode.StartsWith("--smoke-test-key-memory-"))
            {
                const string first = "winui-memory-test-记忆", updated = "winui-memory-test-updated";
                OpenKey();
                await UntilAsync(() => apiKeyInput!.IsLoaded);
                await Task.Delay(80);
                switch (mode["--smoke-test-key-memory-".Length..])
                {
                    case "write": Check(Model.ApiKey.Length == 0, "Test credential was not empty."); apiKeyInput!.Text = first; await UntilAsync(() => Model.ApiKey == first); Check(apiKeyStore!.Load() == first, "Key input was not saved."); SetApiKey("temporary-override"); await Task.Delay(50); Check(apiKeyStore.Load() == first, "Temporary override replaced the saved Key."); break;
                    case "read":
                        Check(Model.ApiKey == first && apiKeyInput!.Text == first, "Key was not restored after restart.");
                        await SaveScreenshotAsync("api-key-header", header);
                        await SaveScreenshotAsync("api-key-plain", (FrameworkElement)KeyPopup!.Child);
                        apiKeyInput!.Text = updated; await UntilAsync(() => Model.ApiKey == updated); Check(apiKeyStore!.Load() == updated, "Updated Key was not saved."); break;
                    case "clear": Check(Model.ApiKey == updated, "Updated Key was not restored."); apiKeyInput!.Text = ""; await UntilAsync(() => Model.ApiKey.Length == 0); Check(apiKeyStore!.Load().Length == 0, "Clearing input did not delete the saved Key."); break;
                    case "empty": Check(Model.ApiKey.Length == 0, "Deleted Key reappeared after restart."); break;
                }
                ClosePopups();
            }
            else if (mode is "--smoke-test-camera" or "--smoke-test-live")
            {
                string camera = Environment.GetEnvironmentVariable("XMAX_SMOKE_CAMERA") ?? "MacBook";
                Model.SelectedCamera = Model.Cameras.First(item => item.Name.Contains(camera, StringComparison.OrdinalIgnoreCase));
                if (mode == "--smoke-test-camera") await Model.PreviewAsync();
                else
                {
                    Model.StartCommand.Execute(null);
                    Check(Model.Busy && !Model.Active && Model.StartLabel == "Starting…", "Startup did not stay on the loading landing page.");
                    await SaveScreenshotAsync("startup-landing");
                    await UntilAsync(() => !Model.Busy, 90);
                    Check(Model.Error.Length == 0, Model.Error);
                    Check(pageEntryStates.Count == 1 && pageEntryStates[0] is RealtimeState.Connected or RealtimeState.Generating,
                        "The generation page opened before RTC connected.");
                }
                await UntilAsync(() => LocalView.PresentedFrameCount >= 10);
                if (mode == "--smoke-test-live")
                {
                    await UntilAsync(() => RemoteView.PresentedFrameCount >= 10);
                    await ClickReferenceAsync(Model.CharacterReferences.First(item => item.Id == "charx-9"));
                    long before = RemoteView.PresentedFrameCount;
                    await UntilAsync(() => RemoteView.PresentedFrameCount >= before + 10);
                    referenceUpdatePassed = true; await Task.Delay(700); await SaveScreenshotAsync("live-reference-update");
                    Model.SelectedMode = "free"; Model.Prompt = "改为柔和的水彩画风格，保持人物和动作一致。";
                    await Model.SubmitPromptAsync().WaitAsync(TimeSpan.FromSeconds(45)); Check(Model.Error.Length == 0, Model.Error);
                }
                Check(Model.Error.Length == 0, Model.Error);
                await SaveScreenshotAsync("local-video", LocalView); if (mode == "--smoke-test-live") await SaveScreenshotAsync("remote-video", RemoteView);
            }
            await SaveScreenshotAsync(mode.TrimStart('-'));
            passed = true;
        }
        catch (Exception failure) { error = failure.ToString(); }
        finally
        {
            Model.PropertyChanged -= ObservePageEntry;
            long localFrames = LocalView.PresentedFrameCount, remoteFrames = RemoteView.PresentedFrameCount;
            uint localWidth = LocalView.VideoWidth, localHeight = LocalView.VideoHeight, remoteWidth = RemoteView.VideoWidth, remoteHeight = RemoteView.VideoHeight;
            string startupTiming = Model.TimingRows[^1].Value;
            ClosePopups();
            try { await Model.StopAsync(); } catch (Exception failure) { passed = false; error ??= failure.Message; }
            var report = new { mode, passed, referenceUpdatePassed, pageEntryStates = pageEntryStates.Select(state => state.ToString()), camera = Model.SelectedCamera?.Name, localFrames, remoteFrames,
                localWidth, localHeight, remoteWidth, remoteHeight, startupTiming, transitions, error };
            string? output = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
            if (!string.IsNullOrEmpty(output)) { Directory.CreateDirectory(output); await File.WriteAllTextAsync(Path.Combine(output, mode.TrimStart('-') + ".json"), JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true })); }
            closed = true; Close(); Application.Current.Exit();
        }
    }
    private async Task<object> VerifyTransitionAsync(bool active)
    {
        FrameworkElement target = active ? workspace : landing;
        await Task.Delay(100);
        bool animations = new Windows.UI.ViewManagement.UISettings().AnimationsEnabled;
        // A newly revealed page can start composing after the first dispatcher delay.
        if (animations && target.Opacity == 0) await UntilAsync(() => target.Opacity > 0, 2);
        double opacity = target.Opacity, offset = ((TranslateTransform)target.RenderTransform).Y;
        await SaveScreenshotAsync(active ? "transition-generation" : "transition-landing");
        if (animations) Check(opacity > 0 && opacity < 1 && offset > 0 && offset < 14, $"Page entry is not fading and moving (opacity={opacity}, offset={offset}).");
        await Task.Delay(500);
        Check(target.Opacity == 1 && ((TranslateTransform)target.RenderTransform).Y == 0, "Page entry did not settle.");
        return new { page = active ? "generation" : "landing", sampledOpacity = opacity, sampledOffset = offset, durationMilliseconds = 450, offsetDip = 14, settled = true };
    }
    private async Task VerifyFramesAsync()
    {
        using var client = new XmaxClient(); await using var realtime = client.CreateRealtime();
        using var sink = await realtime.BindExternalAsync(new(16, 8));
        Model.StatisticsVisible = false;
        LocalView.Width = 160; LocalView.Height = 80; LocalView.VideoSource = realtime.LocalVideo;
        byte[] pixels = new byte[64 * 8];
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) { int i = y * 64 + x * 4; pixels[i + (x < 8 ? 2 : 0)] = 255; pixels[i + 3] = 255; }
        sink.PushBgra(pixels, 64, 1000); await UntilAsync(() => LocalView.PresentedFrameCount >= 1); await Task.Delay(100);
        await VerifyFramePixelsAsync(false); await SaveScreenshotAsync("render-bgra", LocalView);
        await realtime.SetVideoMirrorAsync(new(true, false, false)); sink.PushBgra(pixels, 64, 2000);
        await UntilAsync(() => LocalView.PresentedFrameCount >= 2); await Task.Delay(100); await VerifyFramePixelsAsync(true); await SaveScreenshotAsync("render-mirrored", LocalView);
        await Task.Run(() => { for (int i = 3; i <= 100; ++i) sink.PushBgra(pixels, 64, i * 1000); });
        await UntilAsync(() => LocalView.LastPresentedTimestampMicroseconds == 100000);
        localPanel.Children.Remove(LocalView); await Task.Delay(50); localPanel.Children.Insert(0, LocalView);
        sink.PushBgra(pixels, 64, 101000); await UntilAsync(() => LocalView.LastPresentedTimestampMicroseconds == 101000);
        LocalView.VideoSource = null;
    }
    private async Task VerifyFramePixelsAsync(bool mirrored)
    {
        var bitmap = new RenderTargetBitmap(); await bitmap.RenderAsync(LocalView, 160, 80);
        using var reader = DataReader.FromBuffer(await bitmap.GetPixelsAsync()); byte[] bytes = new byte[reader.UnconsumedBufferLength]; reader.ReadBytes(bytes);
        int left = (bitmap.PixelHeight / 2 * bitmap.PixelWidth + bitmap.PixelWidth / 8) * 4;
        int right = (bitmap.PixelHeight / 2 * bitmap.PixelWidth + bitmap.PixelWidth * 7 / 8) * 4;
        Check(bytes[left + (mirrored ? 0 : 2)] > 200 && bytes[right + (mirrored ? 2 : 0)] > 200, "WinUI displayed pixels or mirror metadata differ from the input.");
    }
}
