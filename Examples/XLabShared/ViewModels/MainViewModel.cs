//
//  MainViewModel.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

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
    private RealtimeMediaStream? localStream;
    internal XmaxRealtime? Session => session;
    private CancellationTokenSource? operationCancellation;
    private int revision;
    private int foregroundOperations;
    private bool busy;
    private string status = "准备就绪 · 摄像头预览不需要 API Key", error = "", metrics = "等待视频统计";
    private CameraDevice? camera;
    private VideoFormat format;
    private string sessionKey = "", sessionCamera = "";
    private RealtimeModel sessionModel;
    private const XmaxEnvironment ServiceEnvironment = XmaxEnvironment.China;
    private VideoFormat sessionFormat;
    private RealtimeVideoTrack? local, remote;
    public ObservableCollection<CameraDevice> Cameras { get; } = [];
    public ObservableCollection<VideoFormat> Formats { get; } = [];
    public static IReadOnlyList<RealtimeModel> Models { get; } = Array.AsReadOnly(Enum.GetValues<RealtimeModel>());
    private RealtimeModel selectedModel = RealtimeModel.X2_1;
    private string modelDisplayName = "X2.1";
    public RealtimeModel SelectedModel
    {
        get => selectedModel;
        set
        {
            if (!CanEdit || Active || selectedModel == value || !Models.Contains(value)) return;
            var info = XmaxClient.GetModelInfo(value);
            selectedModel = value;
            modelDisplayName = info.DisplayName;
            Formats.Clear();
            foreach (var item in info.SupportedFormats) Formats.Add(item);
            SelectedFormat = info.DefaultCameraFormat;
            Changed(); Changed(nameof(ModelDisplayName));
        }
    }
    public string ModelDisplayName => modelDisplayName;
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
    public RealtimeVideoTrack? LocalVideo { get => local; private set { local = value; Changed(); } }
    public RealtimeVideoTrack? RemoteVideo { get => remote; private set { if (remote == value) return; remote = value; Changed(); } }
    public AsyncCommand RefreshCommand { get; }
    public AsyncCommand PreviewCommand { get; }
    public AsyncCommand GenerateCommand { get; }
    public AsyncCommand StopCommand { get; }
    public event PropertyChangedEventHandler? PropertyChanged;

    public MainViewModel(IUiDispatcher dispatcher, IThumbnailLoader thumbnails, string? beautyPreferencesPath = null)
    {
        this.dispatcher = dispatcher;
        this.thumbnails = thumbnails;
        InitializeBeauty(beautyPreferencesPath);
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
            var model = XmaxClient.GetModelInfo(SelectedModel);
            foreach (var item in model.SupportedFormats) Formats.Add(item);
            SelectedFormat = model.DefaultCameraFormat;
            if (!IsPreview) await GuardAsync(RefreshCamerasAsync);
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
        if (session is not null && localStream is not null && sessionKey == ApiKey &&
            sessionCamera == SelectedCamera.Id && sessionFormat == SelectedFormat && sessionModel == SelectedModel)
            return;
        await StopAsync();
        int current = ++revision;
        using var client = new XmaxClient(new() { ApiKey = ApiKey, Environment = ServiceEnvironment });
        var next = client.CreateRealtime(new() { Model = SelectedModel });
        session = next;
        sessionKey = ApiKey; sessionCamera = SelectedCamera.Id; sessionFormat = SelectedFormat; sessionModel = SelectedModel;
        LocalVideo = RemoteVideo = null;
        foreach (var row in TimingRows.Concat(UplinkRows).Concat(UplinkNetworkRows).Concat(DownlinkRows).Concat(DownlinkNetworkRows)) row.Value = "—";
        var previewReady = new TaskCompletionSource<RealtimeState>(TaskCreationOptions.RunContinuationsAsynchronously);
        await next.SetStateListenerAsync(state =>
        {
            if (current != revision) return;
            if (state.ConnectionState == RealtimeConnectionState.Ready || state.Reason is RealtimeReason.Failure)
                previewReady.TrySetResult(state);
            ReceiveState(state);
        });
        await next.SetUplinkVideoStatisticsListenerAsync(stats => { if (current == revision) ReceiveUplinkStatistics(stats); });
        await next.SetDownlinkVideoStatisticsListenerAsync(stats => { if (current == revision) ReceiveDownlinkStatistics(stats); });
        next.EventReceived += item => dispatcher.InvokeAsync(() => { if (current == revision) Receive(item); });
        next.CallbackFaulted += failure => dispatcher.InvokeAsync(() => { if (current == revision) ReportError(failure); });
        RefreshCommands();
        Status = "正在准备摄像头…";
        var preparation = new CancellationTokenSource();
        operationCancellation = preparation;
        var preparationToken = preparation.Token;
        try
        {
            await BindBeautyAsync(next, current);
            preparationToken.ThrowIfCancellationRequested();
            var cameraStream = await next.CreateLocalCameraStreamAsync(
                videoFormat: SelectedFormat,
                deviceId: SelectedCamera.Id,
                mirror: true,
                microphone: Microphone ? RealtimeMicrophone.SystemDefault : RealtimeMicrophone.Disabled,
                cancellationToken: preparationToken
            );
            if (current != revision) throw new OperationCanceledException(preparationToken);
            LocalVideo = cameraStream.VideoTrack;
            var ready = await previewReady.Task.WaitAsync(preparationToken);
            if (ready.Reason is RealtimeReason.Failure failure) throw failure.Error;
            if (current != revision) throw new OperationCanceledException(preparationToken);
            localStream = cameraStream;
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
    public async Task StopAsync()
    {
        if (closingSession is { } closing)
        {
            await closing.Task;
            return;
        }
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        closingSession = completion;
        try
        {
            BeginBeautyStop();
            Active = false;
            pendingConditions = null;
            ++intent;
            elapsedTimer.Stop();
            foreach (var upload in uploadCancellations.Values.ToArray()) upload.Cancel();
            ++revision;
            var previous = session;
            session = null;
            localStream = null;
            var cancellation = operationCancellation;
            operationCancellation = null;
            if (cancellation is not null) await cancellation.CancelAsync();
            await StopRecordingAsync();
            LocalVideo = RemoteVideo = null;
            Status = previous is null ? "准备就绪" : "正在关闭会话…";
            RefreshCommands();
            try { if (previous is not null) await CloseRealtimeAsync(previous); }
            finally { cancellation?.Dispose(); Status = "会话已关闭"; }
        }
        catch (Exception failure) { completion.TrySetException(failure); }
        finally
        {
            EndBeautyStop();
            closingSession = null;
            completion.TrySetResult();
        }
        await completion.Task;
    }

    private void Receive(RealtimeEvent item)
    {
        if (item.Type == RealtimeEventType.State) return;
        UpdateMetricRows(item);
        if (item.Type == RealtimeEventType.Error) Error = $"{item.Message} ({item.Code}, HTTP {item.HttpStatus})";
        else if (item.Type == RealtimeEventType.Timing && item.Code >= 0)
            Metrics = $"首帧 {item.FirstFrameMilliseconds:F0} ms  ·  总启动 {item.TotalMilliseconds:F0} ms";
    }
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
