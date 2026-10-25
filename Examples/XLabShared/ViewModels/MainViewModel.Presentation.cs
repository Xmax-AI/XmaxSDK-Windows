using System.Collections.ObjectModel;
using System.Diagnostics;
using XLab.Shared.Platform;
using XLab.Shared.Services;
using Xmax.SDK;

namespace XLab.Shared.ViewModels;

internal sealed partial class MainViewModel
{
    private bool active, statisticsVisible = true, remoteReady, preview;
    private string selectedMode = "charx";
    private double volume;
    private long intent;
    private DateTimeOffset? startedAt;
    private readonly IUiTimer elapsedTimer;
    private (GenerationContext Context, long Intent, int Revision)? pendingConditions;
    private Task? updateTask;
    internal Task WaitForConditionsAsync() => updateTask ?? Task.CompletedTask;
    private readonly Dictionary<ReferenceItem, CancellationTokenSource> uploadCancellations = [];
    public bool Active { get => active; private set { if (active == value) return; active = value; Changed(); Changed(nameof(StartLabel)); } }
    public bool IsPreview { get => preview; private set { preview = value; Changed(); Changed(nameof(StartLabel)); Changed(nameof(StopLabel)); Changed(nameof(Elapsed)); Changed(nameof(LocalPlaceholder)); } }
    public bool CanEdit => !Busy;
    public bool RemoteReady { get => remoteReady; private set { remoteReady = value; Changed(); Changed(nameof(ResultPlaceholder)); } }
    public string LocalPlaceholder => IsPreview ? "UI Preview\nCamera is disabled" : "";
    public string ResultPlaceholder => IsPreview ? "UI Preview\nGeneration is disabled" : RemoteReady ? "" : State == RealtimeState.Connected ? "Generating…" : "Select a reference to generate…";
    public string LocalTitle => IsPreview ? "Local" : string.IsNullOrEmpty(SelectedCamera?.Name) ? "Local" : "Local · " + SelectedCamera.Name;
    public string KeyLabel => ApiKey.Length == 0 ? "" : "****************";
    public string StartLabel => Busy ? "Starting…" : IsPreview ? "Preview UI" : "Start Camera";
    public string StopLabel => IsPreview ? "Exit Preview" : "Stop Session";
    public string Elapsed => IsPreview ? "Preview" : (DateTimeOffset.UtcNow - (startedAt ?? DateTimeOffset.UtcNow)).ToString(@"mm\:ss");
    public bool StatisticsVisible { get => statisticsVisible; set { statisticsVisible = value; Changed(); } }
    public string SelectedMode
    {
        get => selectedMode;
        set { if (selectedMode == value) return; selectedMode = value; ++intent; pendingConditions = null; ClearError(); Changed(); }
    }
    public double Volume
    {
        get => volume;
        set { volume = Math.Clamp(value, 0, 1); Changed(); Changed(nameof(VolumeLabel)); Changed(nameof(VolumeGlyph)); if (session is not null) _ = ApplyVolumeAsync(session, volume); }
    }
    public string VolumeLabel => $"Remote volume                         {Volume:P0}";
    public string VolumeGlyph => Volume > 0 ? "volume" : "muted";
    public ObservableCollection<ReferenceItem> CharacterReferences { get; } = [];
    public ObservableCollection<ReferenceItem> ClothingReferences { get; } = [];
    public ObservableCollection<ReferenceItem> StyleReferences { get; } = [];
    private ReferenceItem? freeReference;
    public ReferenceItem? FreeReference
    {
        get => freeReference;
        private set
        {
            if (freeReference is not null) freeReference.PropertyChanged -= FreeReferenceChanged;
            freeReference = value;
            if (value is not null) value.PropertyChanged += FreeReferenceChanged;
            Changed(); Changed(nameof(HasFreeReference)); SubmitPromptCommand?.Refresh();
        }
    }
    public bool HasFreeReference => FreeReference is not null;
    private void FreeReferenceChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e) => SubmitPromptCommand.Refresh();
    public MetricRow[] TimingRows { get; } = [new("Create session"), new("RTC join"), new("Publish local stream"), new("First frame"), new("Total startup")];
    public MetricRow[] UplinkRows { get; } = [new("Uplink resolution"), new("Uplink frame rate"), new("Uplink bitrate")];
    public MetricRow[] UplinkNetworkRows { get; } = [new("Uplink packet loss"), new("Uplink network quality"), new("Uplink RTT")];
    public MetricRow[] DownlinkRows { get; } = [new("Downlink resolution"), new("Downlink frame rate"), new("Downlink bitrate")];
    public MetricRow[] DownlinkNetworkRows { get; } = [new("Playback buffer delay"), new("Downlink packet loss"), new("Downlink network quality"), new("RTT (cloud)"), new("E2E (RTC estimate)")];
    public AsyncCommand StartCommand { get; private set; } = null!;
    public AsyncCommand SubmitPromptCommand { get; private set; } = null!;
    public event Action? ApiKeyRequired;
    public event Action? CameraRequired;
    private void InitializePresentation()
    {
        foreach (string mode in new[] { "charx", "clothx", "vibex" }) References(mode).Add(new() { IsUpload = true, Mode = mode, Name = "Upload" });
        foreach (var item in ReferenceCatalog.Load()) References(item.Mode).Add(item);
        StartCommand = new(() => GuardAsync(StartLandingAsync), () => !Busy);
        SubmitPromptCommand = new(() => GuardAsync(SubmitPromptAsync), CanSubmitPrompt);
        elapsedTimer.Tick += (_, _) => Changed(nameof(Elapsed));
    }
    public void ConfigurePreview(bool sessionPage)
    {
        IsPreview = true;
        Active = sessionPage;
        if (sessionPage) MarkSelected(CharacterReferences.First(item => !item.IsUpload));
        Changed(nameof(LocalTitle));
    }
    public async Task StartLandingAsync()
    {
        if (IsPreview) { Active = true; MarkSelected(CharacterReferences.First(item => !item.IsUpload)); return; }
        if (string.IsNullOrWhiteSpace(ApiKey)) { RequireKey(); return; }
        if (SelectedCamera is null) { RequireCamera(); return; }
        var first = CharacterReferences.First(item => !item.IsUpload);
        SelectedMode = "charx";
        MarkSelected(first);
        string freePrompt = Prompt;
        Prompt = ModePrompt("charx"); ReferenceImage = first.ReferenceUrl; FaceRestoration = false;
        try { await GenerateAsync(); }
        catch { await StopAsync(); throw; }
        finally { Prompt = freePrompt; Changed(nameof(Prompt)); }
    }
    private void RequireKey()
    { Error = "API Key is required — click the key button in the top-right corner to enter it."; ApiKeyRequired?.Invoke(); }
    private void RequireCamera()
    {
        Error = Cameras.Count == 0 ? "No camera is available. Connect a camera and try again." : "Select a camera before starting. Choose your physical camera for live video.";
        CameraRequired?.Invoke();
    }
    private void BeginElapsed()
    { if (startedAt is null || !elapsedTimer.IsEnabled) startedAt = DateTimeOffset.UtcNow; elapsedTimer.Start(); Changed(nameof(Elapsed)); }
    public void ClearError() => Error = "";
    private ObservableCollection<ReferenceItem> References(string mode) => mode switch { "charx" => CharacterReferences, "clothx" => ClothingReferences, _ => StyleReferences };
    private IEnumerable<ReferenceItem> AllReferences => CharacterReferences.Concat(ClothingReferences).Concat(StyleReferences).Concat(FreeReference is { } reference ? new[] { reference } : []);
    private void MarkSelected(ReferenceItem selected)
    { foreach (var item in AllReferences) item.IsSelected = item == selected; }
    private static string ModePrompt(string mode) => mode switch { "charx" => "视频中角色替换成参考图中角色", "clothx" => "视频中人物衣服替换成参考图中衣服", _ => "视频风格变为参考图指定的风格" };
    public async Task SelectReferenceAsync(ReferenceItem item)
    {
        if (item.IsUpload || item.IsUploading || !Active) return;
        ClearError();
        MarkSelected(item);
        if (IsPreview) return;
        if (string.IsNullOrWhiteSpace(ApiKey)) { RequireKey(); return; }
        string text = item.Mode == "free" ? Prompt : ModePrompt(item.Mode);
        await QueueConditionsAsync(new(text, item.ReferenceUrl, item.Mode == "clothx"));
    }
    public async Task SubmitPromptAsync()
    {
        if (!FreeReferenceReady) return;
        if (IsPreview) return;
        if (string.IsNullOrWhiteSpace(ApiKey)) { RequireKey(); return; }
        if (string.IsNullOrWhiteSpace(Prompt)) throw new InvalidOperationException("Describe what you want to see first.");
        await QueueConditionsAsync(new(Prompt, FreeReference?.ReferenceUrl));
    }
    private bool FreeReferenceReady => FreeReference is null || !FreeReference.IsUploading && !FreeReference.HasError && FreeReference.ReferenceUrl.Length > 0;
    private bool CanSubmitPrompt() => !Busy && FreeReferenceReady;
    private Task QueueConditionsAsync(GenerationContext context)
    {
        pendingConditions = (context, ++intent, revision);
        if (updateTask is null || updateTask.IsCompleted) updateTask = ApplyConditionsAsync();
        return updateTask;
    }
    private async Task ApplyConditionsAsync()
    {
        while (pendingConditions is { } pending)
        {
            pendingConditions = null;
            var current = session;
            if (current is null || pending.Revision != revision) continue;
            try
            {
                if (State == RealtimeState.Generating) await current.UpdateAsync(pending.Context);
                else await current.StartAsync(pending.Context, Microphone);
                if (pending.Revision == revision) { State = RealtimeState.Generating; RemoteReady = true; }
            }
            catch (Exception failure) when (pending.Revision == revision && pending.Intent == intent) { ReportError(failure); }
            catch (Exception) when (pending.Revision != revision || pending.Intent != intent) { }
        }
    }
    public async Task UploadReferenceAsync(string path, ReferenceItem? existing = null)
    {
        if (existing?.IsUploading == true) return;
        string mode = SelectedMode;
        if (IsPreview) { Error = "Reference uploads require a live session."; return; }
        if (string.IsNullOrWhiteSpace(ApiKey)) { RequireKey(); return; }
        var item = existing ?? new ReferenceItem { Id = Guid.NewGuid().ToString("N"), Name = System.IO.Path.GetFileNameWithoutExtension(path), Mode = mode, ThumbnailUrl = path, LocalPath = path };
        item.IsUploading = true; item.HasError = false;
        if (existing is null)
        {
            if (mode == "free") FreeReference = item;
            else References(mode).Insert(1, item);
        }
        var cancellation = new CancellationTokenSource();
        uploadCancellations.Add(item, cancellation);
        int current = revision;
        try
        {
            try { item.Thumbnail = await thumbnails.LoadAsync(path); } catch (Exception) { /* uploads can still accept encoded WebP without a local codec */ }
            using var client = new XmaxClient(new() { ApiKey = sessionKey, Environment = ServiceEnvironment });
            await using var storage = client.CreateStorage();
            var uploaded = await storage.UploadFileAsync(path, cancellationToken: cancellation.Token);
            item.ReferenceUrl = uploaded.Url; item.IsUploading = false;
            if (current == revision && mode == SelectedMode && (mode != "free" || FreeReference == item)) await SelectReferenceAsync(item);
        }
        catch (OperationCanceledException) { item.IsUploading = false; item.HasError = true; item.Error = "Upload canceled — click to retry."; }
        catch (Exception failure) { item.IsUploading = false; item.HasError = true; item.Error = failure.Message; ReportError(failure); }
        finally { uploadCancellations.Remove(item); cancellation.Dispose(); }
    }
    public void RemoveFreeReference()
    {
        if (FreeReference is { } item && uploadCancellations.TryGetValue(item, out var cancellation)) cancellation.Cancel();
        FreeReference = null;
        ++intent; pendingConditions = null;
    }
    private async Task ApplyVolumeAsync(XmaxRealtime current, double value)
    { try { await current.SetRemoteVolumeAsync((float)value); } catch (Exception failure) { if (current == session) ReportError(failure); } }
    private void UpdateMetricRows(RealtimeEvent item)
    {
        if (item.Type == RealtimeEventType.Timing)
        {
            double[] values = [item.SessionMilliseconds, item.JoinMilliseconds, item.PublishMilliseconds, item.FirstFrameMilliseconds, item.TotalMilliseconds];
            for (int i = 0; i < values.Length; ++i) TimingRows[i].Value = values[i] < 0 ? "—" : $"{values[i]:F1} ms";
        }
        if (item.Type != RealtimeEventType.Statistics) return;
        var video = item.Direction == 0 ? UplinkRows : DownlinkRows;
        var network = item.Direction == 0 ? UplinkNetworkRows : DownlinkNetworkRows;
        if (item.Width > 0 && item.Height > 0) video[0].Value = $"{item.Width}×{item.Height}";
        if (item.FramesPerSecond >= 0) video[1].Value = $"{item.FramesPerSecond} fps";
        if (item.BitrateKbps >= 0) video[2].Value = $"{item.BitrateKbps} kbps";
        if (item.PacketLossPercent >= 0) network[item.Direction == 0 ? 0 : 1].Value = $"{item.PacketLossPercent}%";
        if (item.Quality >= 0) network[item.Direction == 0 ? 1 : 2].Value = item.Quality.ToString();
        if (item.Direction == 1 && item.EndToEndMilliseconds >= 0) network[4].Value = $"{item.EndToEndMilliseconds} ms";
    }
}
