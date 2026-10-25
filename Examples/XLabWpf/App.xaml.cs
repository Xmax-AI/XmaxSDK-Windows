using XLab.Shared.Services;
using System.IO;
using System.Text.Json;
using System.Windows;
using XLabWpf.Views;
using XLabWpf.Services;
using Xmax.SDK;

namespace XLabWpf;

public partial class App : Application
{
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        bool preview = e.Args.Contains("--preview") || e.Args.Contains("--preview-session") || e.Args.Contains("--smoke-test-ui");
        string? mode = e.Args.FirstOrDefault(arg => arg.StartsWith("--smoke-test"));
        ApiKeyStore? store = preview || mode is not null ? null : new();
        // Persistence tests use their own credential; other smokes never write user credentials.
        if (mode?.StartsWith("--smoke-test-key-memory-") == true)
        {
            var id = Guid.Parse(Environment.GetEnvironmentVariable("XMAX_SMOKE_CREDENTIAL_ID") ?? "");
            store = new ApiKeyStore("XMAX.AI/XLabWpf/ApiKey.Smoke." + id.ToString("N"));
        }
        var window = new MainWindow(store);
        if (preview) { window.Model.ConfigurePreview(e.Args.Contains("--preview-session")); window.Title = "XLab - WPF — UI Preview (no live session)"; }
        MainWindow = window;
        window.Show();
        window.LoadApiKey(!preview && (mode is null or "--smoke-test-camera" or "--smoke-test-live") ? Environment.GetEnvironmentVariable("XMAX_API_KEY") : null);
        await window.Model.InitializeAsync();
        if (mode is not null) await SmokeAsync(window, mode);
    }
    private async Task SmokeAsync(MainWindow window, string mode)
    {
        int result = 0;
        string? failure = null;
        long localFrames = 0, remoteFrames = 0;
        uint localWidth = 0, localHeight = 0, remoteWidth = 0, remoteHeight = 0;
        long remoteMediaVideoPackets = 0;
        int localMinimumLuma = -1, localMaximumLuma = -1;
        bool referenceUpdatePassed = false;
        var pageEntryStates = new List<RealtimeState>();
        void ObservePageEntry(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
        {
            if (args.PropertyName == nameof(window.Model.Active) && window.Model.Active && !window.Model.IsPreview) pageEntryStates.Add(window.Model.State);
        }
        window.Model.PropertyChanged += ObservePageEntry;
        try
        {
            string? cameraName = Environment.GetEnvironmentVariable("XMAX_SMOKE_CAMERA");
            if (!string.IsNullOrEmpty(cameraName) && mode is "--smoke-test-camera" or "--smoke-test-live") window.Model.SelectedCamera = window.Model.Cameras.FirstOrDefault(camera => camera.Name.Contains(cameraName, StringComparison.OrdinalIgnoreCase)) ?? throw new InvalidOperationException("Requested test camera was not found.");
            if (mode == "--smoke-test") await window.VerifyMenusAsync(Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT"));
            if (mode.StartsWith("--smoke-test-key-memory-")) window.VerifyApiKeyMemory(mode["--smoke-test-key-memory-".Length..]);
            if (mode == "--smoke-test-ui")
            {
                await VerifyUiAsync(window);
            }
            if (window.Model.Error.Length != 0) throw new InvalidOperationException(window.Model.Error);
            if (mode == "--smoke-test-camera")
            {
                if (mode == "--smoke-test-camera")
                {
                    var startup = window.Model.PreviewAsync();
                    await Task.Delay(30);
                    await window.Model.StopAsync();
                    try { await startup; } catch (OperationCanceledException) { }
                    if (window.Model.State != RealtimeState.Idle || window.Model.Active) throw new InvalidOperationException("Stopping startup did not restore the idle landing page.");
                }
                await window.Model.PreviewAsync();
                await UntilAsync(() => window.LocalVideoView.PresentedFrameCount >= 10, TimeSpan.FromSeconds(20));
                localFrames = window.LocalVideoView.PresentedFrameCount;
                localWidth = window.LocalVideoView.VideoWidth; localHeight = window.LocalVideoView.VideoHeight;
            }
            if (mode == "--smoke-test-key-gate")
            {
                window.SetApiKey("");
                await window.Model.StartLandingAsync();
                if (window.Model.Active || !window.KeyPopupOpen) throw new InvalidOperationException("The missing-key gate did not open the API Key popup.");
                await Task.Delay(150);
                string? keyOutput = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
                if (!string.IsNullOrEmpty(keyOutput)) window.SaveKeyPopupScreenshot(Path.Combine(keyOutput, "api-key-popup.png"));
            }
            if (mode == "--smoke-test-live")
            {
                // Exercise the same command as Start Camera, without preceding preview/preparation.
                window.Model.StartCommand.Execute(null);
                if (!window.Model.Busy || window.Model.Active || window.Model.StartLabel != "Starting…")
                    throw new InvalidOperationException("Startup did not stay on the loading landing page.");
                string? startupOutput = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
                if (!string.IsNullOrEmpty(startupOutput)) window.SaveUiScreenshot(Path.Combine(startupOutput, "startup-landing.png"));
                await UntilAsync(() => !window.Model.Busy, TimeSpan.FromSeconds(90));
                if (window.Model.Error.Length != 0) throw new InvalidOperationException(window.Model.Error);
                if (pageEntryStates.Count != 1 || pageEntryStates[0] is not (RealtimeState.Connected or RealtimeState.Generating))
                    throw new InvalidOperationException("The generation page opened before RTC connected.");
                if (window.Model.Session is null) throw new InvalidOperationException(window.Model.Error);
                await UntilAsync(() => window.LocalVideoView.PresentedFrameCount >= 10 && window.RemoteVideoView.PresentedFrameCount >= 10, TimeSpan.FromSeconds(20));
                (localMinimumLuma, localMaximumLuma) = Views.MainWindow.VideoLumaRange(window.LocalVideoView);
                if (localMaximumLuma - localMinimumLuma < 8) throw new InvalidOperationException("The selected camera delivers a uniform blank picture. Select a physical camera or start the virtual camera's input.");
                localFrames = window.LocalVideoView.PresentedFrameCount;
                localWidth = window.LocalVideoView.VideoWidth; localHeight = window.LocalVideoView.VideoHeight;
                await window.Model.Session!.SetRemoteMediaListenerAsync(packet =>
                {
                    using (packet)
                    {
                        if (packet.Type == MediaType.Video) Interlocked.Increment(ref remoteMediaVideoPackets);
                    }
                });
                await UntilAsync(() => Interlocked.Read(ref remoteMediaVideoPackets) > 0, TimeSpan.FromSeconds(10));
                remoteFrames = window.RemoteVideoView.PresentedFrameCount;
                remoteWidth = window.RemoteVideoView.VideoWidth; remoteHeight = window.RemoteVideoView.VideoHeight;
                if (window.Model.State != RealtimeState.Generating) throw new InvalidOperationException("Generation did not become active.");
                await UntilAsync(() => window.Model.TimingRows[^1].Value != "—", TimeSpan.FromSeconds(10));
                await UntilAsync(() => window.Model.UplinkRows[0].Value != "—" && window.Model.DownlinkRows[0].Value != "—", TimeSpan.FromSeconds(15));
                long beforeReference = window.RemoteVideoView.PresentedFrameCount;
                await window.ClickReferenceAsync(window.Model.CharacterReferences.First(item => item.Id == "charx-9"));
                await UntilAsync(() => window.RemoteVideoView.PresentedFrameCount > beforeReference + 10, TimeSpan.FromSeconds(15));
                if (window.Model.State != RealtimeState.Generating) throw new InvalidOperationException("Reference update stopped generation.");
                referenceUpdatePassed = true;
                await Task.Delay(800);
                string? referenceOutput = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
                if (!string.IsNullOrEmpty(referenceOutput)) window.SaveUiScreenshot(Path.Combine(referenceOutput, "live-reference-update.png"));
                window.Model.SelectedMode = "free";
                window.Model.Prompt = "改为柔和的水彩画风格，保持人物和动作一致。";
                await window.Model.SubmitPromptAsync().WaitAsync(TimeSpan.FromSeconds(30));
                if (window.Model.Error.Length != 0) throw new InvalidOperationException(window.Model.Error);
            }
            await Task.Delay(300);
            string? output = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
            if (!string.IsNullOrEmpty(output))
            {
                Directory.CreateDirectory(output);
                window.SaveUiScreenshot(Path.Combine(output, mode.TrimStart('-') + ".png"));
            }
        }
        catch (Exception error) { result = 1; failure = error.Message; }
        finally
        {
            window.Model.PropertyChanged -= ObservePageEntry;
            window.DismissPopups();
            object videoDiagnostics = window.VideoDiagnostics;
            string? videoOutput = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
            try { if (!string.IsNullOrEmpty(videoOutput)) window.SaveVideoScreenshots(videoOutput); }
            catch (Exception error) { result = 1; failure ??= error.Message; }
            try { await window.Model.StopAsync(); } catch (Exception error) { result = 1; failure ??= error.Message; }
            string? output = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
            if (!string.IsNullOrEmpty(output))
            {
                Directory.CreateDirectory(output);
                await File.WriteAllTextAsync(Path.Combine(output, mode.TrimStart('-') + ".json"),
                    JsonSerializer.Serialize(new { mode, passed = result == 0, localFrames, remoteFrames,
                        localWidth, localHeight, remoteWidth, remoteHeight, remoteMediaVideoPackets,
                        startupTiming = window.Model.TimingRows[^1].Value,
                        uplinkResolution = window.Model.UplinkRows[0].Value, downlinkResolution = window.Model.DownlinkRows[0].Value,
                        localMinimumLuma, localMaximumLuma, referenceUpdatePassed, pageEntryStates = pageEntryStates.Select(state => state.ToString()), videoDiagnostics, error = failure }, new JsonSerializerOptions { WriteIndented = true }));
            }
            Shutdown(result);
        }
    }
    private static async Task UntilAsync(Func<bool> predicate, TimeSpan timeout)
    {
        DateTime deadline = DateTime.UtcNow + timeout;
        while (!predicate())
        { if (DateTime.UtcNow >= deadline) throw new TimeoutException("No displayed video frames before timeout."); await Task.Delay(50); }
    }
    private static async Task VerifyUiAsync(MainWindow window)
    {
        string? output = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT");
        if (!window.Model.IsPreview || window.Model.Session is not null) throw new InvalidOperationException("UI preview must not create native sessions.");
        await Task.Delay(600);
        if (!string.IsNullOrEmpty(output)) window.SaveUiScreenshot(Path.Combine(output, "aligned-landing.png"));
        List<object> layouts = [];
        foreach (var size in new[] { (Width: 1920d, Height: 1080d, Name: "wide"), (Width: 1180d, Height: 800d, Name: "minimum") })
        {
            window.Width = size.Width; window.Height = size.Height;
            await Task.Delay(200);
            var layout = window.LayoutMetrics;
            if (Math.Abs(layout.PageWidth - layout.Width) > 20 || Math.Abs(layout.HeaderWidth - (layout.PageWidth - 56)) > 2)
                throw new InvalidOperationException("The page did not follow the resized viewport.");
            if (layout.StageWidth + 56 > layout.PageWidth + 2 || layout.StageHeight <= 0)
                throw new InvalidOperationException("The landing preview overflows the resized page.");
            if (layout.StageWidth > 940.5 || Math.Abs(layout.StageWidth * 1024 / 1920 - layout.StageHeight) > .5)
                throw new InvalidOperationException("The landing preview no longer matches the Qt/macOS size cap and aspect ratio.");
            layouts.Add(new { size.Name, layout.Width, layout.PageWidth, layout.HeaderWidth, layout.StageWidth, layout.StageHeight });
            if (!string.IsNullOrEmpty(output)) window.SaveUiScreenshot(Path.Combine(output, "landing-" + size.Name + ".png"));
        }
        if (!string.IsNullOrEmpty(output))
            await File.WriteAllTextAsync(Path.Combine(output, "responsive-layout.json"), JsonSerializer.Serialize(layouts, new JsonSerializerOptions { WriteIndented = true }));
        window.Width = 1360; window.Height = 890;
        await Task.Delay(200);
        await window.Model.StartLandingAsync();
        List<object> transitions = [await window.VerifyPageEntryAsync(true, output)];
        // Images are external and may be unavailable; the UI reports failures and permits retry.
        var deadline = DateTime.UtcNow.AddSeconds(30);
        while (window.Model.CharacterReferences.Any(item => !item.IsUpload && item.Thumbnail is null && !item.HasError) && DateTime.UtcNow < deadline) await Task.Delay(100);
        await window.ClickReferenceAsync(window.Model.CharacterReferences.Where(item => !item.IsUpload).Skip(2).First());
        await window.ClickReferenceAsync(window.Model.ClothingReferences.First(item => !item.IsUpload));
        await window.ClickReferenceAsync(window.Model.StyleReferences.First(item => !item.IsUpload));
        await window.ClickReferenceAsync(window.Model.CharacterReferences.Where(item => !item.IsUpload).Skip(2).First());
        await window.VerifyUploadClickAsync();
        if (!string.IsNullOrEmpty(output)) window.SaveUiScreenshot(Path.Combine(output, "aligned-session.png"));
        window.Model.StatisticsVisible = false;
        window.Model.SelectedMode = "free";
        await Task.Delay(100);
        if (!string.IsNullOrEmpty(output)) window.SaveUiScreenshot(Path.Combine(output, "aligned-free-prompt.png"));
        await window.Model.SubmitPromptAsync();
        if (window.Model.Session is not null) throw new InvalidOperationException("Preview submit created a native session.");
        window.Model.SelectedMode = "clothx";
        if (window.Model.Session is not null) throw new InvalidOperationException("Mode switching created generation work.");
        window.Model.SelectedMode = "charx";
        if (!window.Model.CharacterReferences.Where(item => !item.IsUpload).Skip(2).First().IsSelected) throw new InvalidOperationException("Mode switching lost the selected reference.");
        window.Model.StatisticsVisible = true;
        await window.Model.StopAsync();
        transitions.Add(await window.VerifyPageEntryAsync(false, output));
        if (window.Model.Active) throw new InvalidOperationException("Stop did not return to landing.");
        // Interrupt both directions while entry clocks are still running, then verify the final page.
        await window.Model.StartLandingAsync();
        await Task.Delay(50);
        await window.Model.StopAsync();
        await Task.Delay(50);
        await window.Model.StartLandingAsync();
        transitions.Add(await window.VerifyPageEntryAsync(true, null));
        await window.Model.StopAsync();
        transitions.Add(await window.VerifyPageEntryAsync(false, null));
        if (!string.IsNullOrEmpty(output))
            await File.WriteAllTextAsync(Path.Combine(output, "page-transitions.json"), JsonSerializer.Serialize(transitions, new JsonSerializerOptions { WriteIndented = true }));
    }
}
