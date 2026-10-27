using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using XLab.Shared.Platform;
using Xmax.SDK;
using XLab.Shared.Services;

namespace XLab.Shared.ViewModels;

internal sealed partial class MainViewModel : INotifyPropertyChanged, IAsyncDisposable
{
    private readonly IUiDispatcher dispatcher;
    private readonly IThumbnailLoader thumbnails;
    private XmaxRealtime? session;
    internal XmaxRealtime? Session => session;
    private CancellationTokenSource? operationCancellation;
    private int revision;
    private int foregroundOperations;
    private bool busy;
    private string status = "准备就绪 · 摄像头预览不需要 API Key", error = "", metrics = "等待视频统计";
    private CameraDevice? camera;
    private static readonly VideoFormat ExampleCameraFormat = new(1920, 1080, 30);
    private VideoFormat format = ExampleCameraFormat;
    private string sessionKey = "", sessionCamera = "", sessionModel = "";
    private const XmaxEnvironment ServiceEnvironment = XmaxEnvironment.China;
    private VideoFormat sessionFormat;
    private IXmaxVideoSource? local, remote;
    public ObservableCollection<CameraDevice> Cameras { get; } = [];
    public ObservableCollection<VideoFormat> Formats { get; } = [];
    public static IReadOnlyList<string> Models { get; } = Array.AsReadOnly(new[] { "x2.1" });
    private string selectedModel = Models[0];
    public string SelectedModel
    {
        get => selectedModel;
        set
        {
            if (!CanEdit || Active || selectedModel == value || !Models.Contains(value)) return;
            var info = XmaxClient.GetModelInfo(value);
            selectedModel = value;
            Formats.Clear();
            foreach (var item in info.SupportedFormats) Formats.Add(item);
            SelectedFormat = ExampleCameraFormat;
            Changed(); Changed(nameof(ModelDisplayName));
        }
    }
    public string ModelDisplayName => "X" + SelectedModel[1..];
    private string apiKey = "";
    public string ApiKey { get => apiKey; set { apiKey = value; Changed(); Changed(nameof(KeyLabel)); } }
    public string Prompt { get; set; } = "";
    public string ReferenceImage { get; set; } = "";
    public bool Microphone { get; set; }
    public bool FaceRestoration { get; set; }
    public CameraDevice? SelectedCamera
    {
        get => camera;
        set { camera = value; if (value is not null && !IsPreview) CameraSelectionStore.Save(value.Id); Changed(); Changed(nameof(LocalTitle)); }
    }
    public VideoFormat SelectedFormat { get => format; set { format = value; Changed(); } }
    public bool Busy { get => busy; private set { busy = value; Changed(); Changed(nameof(CanEdit)); Changed(nameof(StartLabel)); RefreshCommands(); } }
    public string Status { get => status; private set { status = value; Changed(); } }
    public string Error { get => error; private set { error = value; Changed(); } }
    public string Metrics { get => metrics; private set { metrics = value; Changed(); } }
    public IXmaxVideoSource? LocalVideo { get => local; private set { local = value; Changed(); } }
    public IXmaxVideoSource? RemoteVideo { get => remote; private set { remote = value; Changed(); } }
    public AsyncCommand RefreshCommand { get; }
    public AsyncCommand PreviewCommand { get; }
    public AsyncCommand GenerateCommand { get; }
    public AsyncCommand StopCommand { get; }
    public RealtimeState State { get; private set; }
    public event PropertyChangedEventHandler? PropertyChanged;

    public MainViewModel(IUiDispatcher dispatcher, IThumbnailLoader thumbnails)
    {
        this.dispatcher = dispatcher;
        this.thumbnails = thumbnails;
        elapsedTimer = dispatcher.CreateTimer(TimeSpan.FromSeconds(1));
        RefreshCommand = new(() => GuardAsync(RefreshCamerasAsync), () => !Busy);
        PreviewCommand = new(() => GuardAsync(PreviewAsync), () => !Busy && Cameras.Count > 0);
        GenerateCommand = new(() => GuardAsync(GenerateAsync), () => !Busy && Cameras.Count > 0);
        StopCommand = new(() => GuardAsync(StopAsync), () => session is not null);
        InitializePresentation();
    }
    public async Task InitializeAsync()
    {
        try
        {
            if (IsPreview) return;
            var model = XmaxClient.GetModelInfo(SelectedModel);
            foreach (var item in model.SupportedFormats) Formats.Add(item);
            SelectedFormat = ExampleCameraFormat;
            await GuardAsync(RefreshCamerasAsync);
        }
        catch (Exception failure) { ReportError(failure); }
    }
    public async Task RefreshCamerasAsync()
    {
        using var client = new XmaxClient();
        var devices = await client.ListCamerasAsync();
        string? previous = SelectedCamera?.Id ?? CameraSelectionStore.Load();
        Cameras.Clear();
        foreach (var item in devices) Cameras.Add(item);
        // Match Qt: preserve an explicit choice, auto-select only when there is exactly one device.
        SelectedCamera = previous is not null ? Cameras.FirstOrDefault(item => item.Id == previous) : Cameras.Count == 1 ? Cameras[0] : null;
        Status = Cameras.Count == 0 ? "未发现摄像头，请检查设备和 Windows 相机权限。" : $"发现 {Cameras.Count} 个摄像头";
        RefreshCommands();
    }
    public async Task PreviewAsync()
    {
        await EnsureSessionAsync();
        Active = true;
        BeginElapsed();
    }
    private async Task EnsureSessionAsync()
    {
        if (SelectedCamera is null) { RequireCamera(); throw new InvalidOperationException("Select a camera before starting. Choose your physical camera for live video."); }
        if (session is not null && sessionKey == ApiKey &&
            sessionCamera == SelectedCamera.Id && sessionFormat == SelectedFormat && sessionModel == SelectedModel && State != RealtimeState.Idle)
            return;
        await StopAsync();
        int current = ++revision;
        using var client = new XmaxClient(new() { ApiKey = ApiKey, Environment = ServiceEnvironment });
        var next = client.CreateRealtime(new() { Model = SelectedModel });
        session = next;
        sessionKey = ApiKey; sessionCamera = SelectedCamera.Id; sessionFormat = SelectedFormat; sessionModel = SelectedModel;
        LocalVideo = next.LocalVideo; RemoteVideo = next.RemoteVideo;
        foreach (var row in TimingRows.Concat(UplinkRows).Concat(UplinkNetworkRows).Concat(DownlinkRows).Concat(DownlinkNetworkRows)) row.Value = "—";
        next.EventReceived += item => dispatcher.InvokeAsync(() => { if (current == revision) Receive(item); });
        next.CallbackFaulted += failure => dispatcher.InvokeAsync(() => { if (current == revision) ReportError(failure); });
        RefreshCommands();
        Status = "正在准备摄像头…";
        var preparation = new CancellationTokenSource();
        operationCancellation = preparation;
        var preparationToken = preparation.Token;
        try
        {
            await next.SetVideoMirrorAsync(new(true, true, false));
            preparationToken.ThrowIfCancellationRequested();
            await next.PrepareCameraAsync(SelectedCamera.Id, SelectedFormat, Microphone, preparationToken);
            if (current != revision) throw new OperationCanceledException(preparationToken);
            State = RealtimeState.Ready;
            Status = "摄像头已就绪 · 本地预览中";
        }
        catch (Exception failure) when (current != revision)
        { throw new OperationCanceledException("Camera preparation was stopped.", failure, preparationToken); }
        catch
        {
            if (current == revision) await StopAsync();
            throw;
        }
        finally { if (current == revision) { operationCancellation?.Dispose(); operationCancellation = null; } }
    }
    public async Task GenerateAsync()
    {
        if (string.IsNullOrWhiteSpace(ApiKey)) throw new InvalidOperationException("实时生成需要 Xmax API Key；摄像头预览可以单独使用。");
        if (string.IsNullOrWhiteSpace(Prompt)) throw new InvalidOperationException("请输入生成提示词。");
        await EnsureSessionAsync();
        var currentSession = session ?? throw new OperationCanceledException();
        int current = revision;
        Status = State == RealtimeState.Generating ? "正在更新生成条件…" : "正在连接服务并等待首张结果帧…";
        operationCancellation = new();
        try
        {
            await currentSession.StartAsync(new(Prompt, string.IsNullOrWhiteSpace(ReferenceImage) ? null : ReferenceImage,
                FaceRestoration), Microphone, operationCancellation.Token);
            if (current == revision) { State = RealtimeState.Generating; Status = "实时生成中"; Active = true; RemoteReady = true; BeginElapsed(); }
        }
        finally { if (current == revision) { operationCancellation?.Dispose(); operationCancellation = null; } }
    }
    public async Task StopAsync()
    {
        Active = false;
        RemoteReady = false;
        pendingConditions = null;
        ++intent;
        elapsedTimer.Stop();
        foreach (var upload in uploadCancellations.Values.ToArray()) upload.Cancel();
        ++revision;
        var previous = session;
        session = null;
        var cancellation = operationCancellation;
        operationCancellation = null;
        if (cancellation is not null) await cancellation.CancelAsync();
        await StopRecordingAsync();
        LocalVideo = RemoteVideo = null;
        State = RealtimeState.Idle;
        Status = previous is null ? "准备就绪" : "正在关闭会话…";
        RefreshCommands();
        try { if (previous is not null) await previous.DisposeAsync(); }
        finally { cancellation?.Dispose(); Status = "会话已关闭"; }
    }
    private void Receive(RealtimeEvent item)
    {
        UpdateMetricRows(item);
        if (item.Type == RealtimeEventType.State)
        {
            State = item.State;
            RemoteReady = item.State == RealtimeState.Generating;
            // Match macOS: keep the landing page visible until RTC is connected.
            if (!Active && item.State is RealtimeState.Connected or RealtimeState.Generating)
            {
                Active = true;
                BeginElapsed();
            }
            Status = $"状态：{item.State}";
            if (!RemoteReady && IsRecording) _ = FinishInterruptedRecordingAsync();
        }
        else if (item.Type == RealtimeEventType.Error) Error = $"{item.Message} ({item.Code}, HTTP {item.HttpStatus})";
        else if (item.Type == RealtimeEventType.Statistics && item.Width > 0 && item.Height > 0)
            Metrics = $"{(item.Direction == 0 ? "上行" : "下行")}  {item.Width} × {item.Height}  ·  {Available(item.FramesPerSecond)} fps  ·  {Available(item.BitrateKbps)} kbps  ·  延迟 {Available(item.EndToEndMilliseconds)} ms";
        else if (item.Type == RealtimeEventType.Timing && item.Code >= 0)
            Metrics = $"首帧 {item.FirstFrameMilliseconds:F0} ms  ·  总启动 {item.TotalMilliseconds:F0} ms";
    }
    private static string Available(int value) => value >= 0 ? value.ToString() : "—";
    private async Task GuardAsync(Func<Task> operation)
    {
        Busy = ++foregroundOperations > 0;
        Error = "";
        try { await operation(); }
        catch (OperationCanceledException) { Status = "操作已取消"; }
        catch (Exception failure) { ReportError(failure); }
        finally { Busy = --foregroundOperations > 0; }
    }
    public void ReportError(Exception failure) => Error = failure is XmaxException sdk
        ? $"{sdk.Message} (SDK {sdk.Result}, API {sdk.ApiCode}, HTTP {sdk.HttpStatus})" : failure.Message;
    private void RefreshCommands()
    { RefreshCommand.Refresh(); PreviewCommand.Refresh(); GenerateCommand.Refresh(); StopCommand.Refresh(); StartCommand?.Refresh(); SubmitPromptCommand?.Refresh(); RecordingChanged(); }
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new(name));
    public ValueTask DisposeAsync() => new(StopAsync());
}
