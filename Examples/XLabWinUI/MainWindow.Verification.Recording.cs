using System.Reflection;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using XLab.Shared.Recording;
using XLab.Shared.ViewModels;
using Xmax.SDK;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    private async Task VerifyRecordingAsync()
    {
        Check(!recordButton.IsEnabled && !Model.CanRecord, "UI preview must not enable live recording.");
        // Use an idle native handle and synthetic packets: no camera, API key, RTC join, or cloud task.
        // Disconnect automatic picker presentation so save-failure/retry can be exercised deterministically.
        Model.PropertyChanged -= ModelChanged;
        using var client = new XmaxClient();
        var native = client.CreateRealtime();
        typeof(MainViewModel).GetField("session", BindingFlags.NonPublic | BindingFlags.Instance)!.SetValue(Model, native);
        typeof(MainViewModel).GetProperty(nameof(Model.IsPreview))!.SetValue(Model, false);
        typeof(MainViewModel).GetProperty(nameof(Model.RemoteReady))!.SetValue(Model, true);
        RefreshUi();
        string? path = null;
        try
        {
            Check(recordButton.IsEnabled && Model.CanRecord, "A ready session cannot start recording.");
            var peer = FrameworkElementAutomationPeer.CreatePeerForElement(recordButton)!;
            ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
            await UntilAsync(() => Model.IsRecording);
            var registration = (Task)typeof(MainViewModel).GetField("recordingRegistration", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(Model)!;
            await registration;
            RefreshUi();
            Check(recordButton.Text == "Stop Recording" && recordButton.Glyph == "record" && recordButton.Accent && recordButton.IsEnabled, "Active recording button state is incorrect.");
            await SaveScreenshotAsync("recording-active", header);
            var recorder = (RemoteMediaRecorder)typeof(MainViewModel).GetField("recorder", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(Model)!;
            recorder.Append(new RecordingVideoFixture(0)); recorder.Append(new RecordingVideoFixture(1_000_000));
            var finishing = Model.StopRecordingAsync(); RefreshUi();
            Check(Model.IsFinishingRecording && recordButton.IsLoading && !recordButton.IsEnabled, "Finishing state permits a second recording.");
            await finishing; RefreshUi();
            Check(Model.HasRecordingToSave && recordButton.Text == "Saving…" && !Model.CanRecord && !recordButton.IsEnabled, "Finalized clip was not retained for saving.");
            path = (string)typeof(MainViewModel).GetField("recordingPath", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(Model)!;
            string output = Environment.GetEnvironmentVariable("XMAX_SMOKE_OUTPUT")!;
            try { await Model.SaveRecordingAsync(Path.Combine(output, Guid.NewGuid().ToString("N"), "missing", "clip.mp4")); throw new Exception("Expected save failure."); }
            catch (DirectoryNotFoundException) { }
            Check(Model.HasRecordingToSave && File.Exists(path), "Save failure discarded the clip.");
            string destination = Path.Combine(output, "winui-recording.mp4");
            await Model.SaveRecordingAsync(destination); RefreshUi();
            Check(!Model.HasRecordingToSave && !File.Exists(path) && File.Exists(destination) && Model.CanRecord, "Save retry did not restore recording availability.");
            // Stop while callback registration is still pending; all accepted frames must be finalized once.
            var starting = Model.StartRecordingAsync();
            recorder = (RemoteMediaRecorder)typeof(MainViewModel).GetField("recorder", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(Model)!;
            recorder.Append(new RecordingVideoFixture(0));
            await Model.StopAsync(); await starting;
            Check(Model.HasRecordingToSave && !Model.IsRecording && !Model.IsFinishingRecording && Model.Session is null,
                "Session shutdown did not finish the active recording.");
            path = (string)typeof(MainViewModel).GetField("recordingPath", BindingFlags.NonPublic | BindingFlags.Instance)!.GetValue(Model)!;
            await Model.DiscardRecordingAsync(); RefreshUi();
            Check(!Model.HasRecordingToSave && !File.Exists(path), "Explicit discard leaked its temporary MP4.");
            Check(!Model.CanRecord && !recordButton.IsEnabled, "Closed session can still record.");
            await SaveScreenshotAsync("recording-stopped", header);
        }
        finally
        {
            await Model.StopAsync(); await Model.DiscardRecordingAsync();
            Model.PropertyChanged += ModelChanged;
        }
    }

    private sealed class RecordingVideoFixture(long microseconds) : IRecordingPacket
    {
        private readonly byte[] pixels = Enumerable.Repeat((byte)127, 96 * 64 * 4).ToArray();
        public MediaType Type => MediaType.Video;
        public ulong StreamId => 1;
        public MediaTime Time => new(microseconds, 1_000_000);
        public MediaTime Duration => new(1, 30);
        public int Width => 96;
        public int Height => 64;
        public int SampleRate => 0;
        public int Channels => 0;
        public int Samples => 0;
        public int ByteCount => pixels.Length;
        public unsafe void ReadVideo(Action<nint, int, int> read) { fixed (byte* data = pixels) read((nint)data, Width * 4, pixels.Length); }
        public void ReadAudio(Action<nint, int> read) => throw new InvalidOperationException();
        public void Dispose() { }
    }
}
