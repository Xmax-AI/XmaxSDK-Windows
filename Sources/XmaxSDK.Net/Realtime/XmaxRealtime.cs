using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;

public sealed partial class XmaxRealtime : IAsyncDisposable
{
    private readonly NativeHandle handle;
    private readonly OperationTracker operations;
    private readonly CancellationTokenSource eventsStop = new();
    private readonly Task eventsWorker;
    public IXmaxVideoSource LocalVideo { get; }
    public IXmaxVideoSource RemoteVideo { get; }
    /// <summary>Delivered serially on a background worker. Dispatch UI updates explicitly.</summary>
    public event Action<RealtimeEvent>? EventReceived;
    public event Action<Exception>? CallbackFaulted;

    internal XmaxRealtime(NativeHandle handle)
    {
        this.handle = handle;
        operations = new(handle);
        LocalVideo = new VideoSource(handle, true);
        RemoteVideo = new VideoSource(handle, false);
        eventsWorker = Task.Run(PumpEventsAsync);
    }
    private Task Run(Action<nint> action, CancellationToken token = default, bool disconnectOnCancel = false) =>
        operations.Run(value => NativeCancellation.Run(token, () => { action(value); return 0; },
            () => { if (disconnectOnCancel) NativeMethods.xmax_realtime_disconnect(value); }), token);

    /// <summary>Cancellation closes local media as well as cancelling camera startup.</summary>
    public Task PrepareCameraAsync(string? deviceId = null, VideoFormat format = default, bool microphone = false,
        CancellationToken cancellationToken = default) => operations.Run(value =>
        NativeCancellation.Run(cancellationToken, () => { PrepareCamera(value, deviceId, format, microphone); return 0; },
            () => NativeMethods.xmax_realtime_close(value)), cancellationToken);
    private static unsafe void PrepareCamera(nint handle, string? device, VideoFormat format, bool microphone)
    {
        using var text = new Utf8(device);
        var config = new xmax_camera_config { struct_size = (uint)sizeof(xmax_camera_config), device_id = text.Pointer,
            format = format.Native, use_microphone = microphone ? 1 : 0 };
        NativeError.Check(NativeMethods.xmax_realtime_prepare_camera(handle, &config));
    }
    public Task PrepareExternalAsync(VideoFormat format) => Run(value => PrepareExternal(value, format));
    private static unsafe void PrepareExternal(nint value, VideoFormat format)
    { var native = format.Native; NativeError.Check(NativeMethods.xmax_realtime_prepare_external(value, &native)); }
    /// <summary>Cancellation disconnects the whole connection; it is not a per-task cancel.</summary>
    public Task ConnectAsync(bool microphone = false, CancellationToken cancellationToken = default) =>
        Run(value => NativeError.Check(NativeMethods.xmax_realtime_connect(value, microphone ? 1 : 0)), cancellationToken, true);
    public Task StartAsync(GenerationContext context, bool microphone = false, CancellationToken cancellationToken = default) =>
        Run(value => Generate(value, context, 0, microphone), cancellationToken, true);
    public Task GenerateAsync(GenerationContext? context = null, CancellationToken cancellationToken = default) =>
        Run(value => Generate(value, context, 1, false), cancellationToken, true);
    public Task UpdateAsync(GenerationContext context, CancellationToken cancellationToken = default) =>
        Run(value => Generate(value, context, 2, false), cancellationToken, true);
    private static unsafe void Generate(nint value, GenerationContext? context, int kind, bool microphone)
    {
        using var prompt = new Utf8(context?.Prompt);
        using var reference = new Utf8(context?.ReferenceImage);
        var native = new xmax_generation_context { struct_size = (uint)sizeof(xmax_generation_context), prompt = prompt.Pointer,
            reference_path = reference.Pointer, face_restoration = context?.FaceRestoration == true ? 1 : 0 };
        NativeError.Check(kind switch
        {
            0 => NativeMethods.xmax_realtime_start(value, &native, microphone ? 1 : 0),
            1 => NativeMethods.xmax_realtime_generate(value, context is null ? null : &native),
            _ => NativeMethods.xmax_realtime_update(value, &native)
        });
    }
    public Task SwitchCameraAsync(string deviceId) => Run(value => SwitchCamera(value, deviceId));
    private static unsafe void SwitchCamera(nint value, string deviceId)
    { using var text = new Utf8(deviceId); NativeError.Check(NativeMethods.xmax_realtime_switch_camera(value, text.Pointer)); }
    public Task StopMediaAsync() => Run(value => NativeError.Check(NativeMethods.xmax_realtime_stop_media(value)));
    public Task DisconnectAsync() => Run(value => NativeError.Check(NativeMethods.xmax_realtime_disconnect(value)));
    public Task CloseAsync() => Run(value => NativeError.Check(NativeMethods.xmax_realtime_close(value)));
    public Task SetRemoteVolumeAsync(float volume) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_remote_volume(value, volume)));
    public Task SetAdaptiveQualityAsync(bool enabled) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_adaptive_quality(value, enabled ? 1 : 0)));
    public Task SetUplinkQualityLevelAsync(int level) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_uplink_quality_level(value, level)));
    public Task SetDownlinkQualityLevelAsync(int level) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_downlink_quality_level(value, level)));
    public Task UpdateCameraFormatAsync(VideoFormat format) => Run(value => UpdateCameraFormat(value, format));
    private static unsafe void UpdateCameraFormat(nint value, VideoFormat format)
    { var native = format.Native; NativeError.Check(NativeMethods.xmax_realtime_update_camera_format(value, &native)); }
    public Task SetEncodingAsync(EncodingConfiguration config) => Run(value => SetEncoding(value, config));
    private static unsafe void SetEncoding(nint value, EncodingConfiguration config)
    {
        var native = new xmax_encoding_config { struct_size = (uint)sizeof(xmax_encoding_config),
            minimum_bitrate_kbps = config.MinimumBitrateKbps, maximum_bitrate_kbps = config.MaximumBitrateKbps, preference = (int)config.Preference };
        NativeError.Check(NativeMethods.xmax_realtime_set_encoding(value, &native));
    }
    public Task SetVideoMirrorAsync(VideoMirrorConfiguration config) => Run(value => SetMirror(value, config));
    private static unsafe void SetMirror(nint value, VideoMirrorConfiguration config)
    {
        var native = new xmax_video_mirror_config { struct_size = (uint)sizeof(xmax_video_mirror_config),
            local_preview = config.LocalPreview ? 1 : 0, remote_view = config.RemoteView ? 1 : 0, published_video = config.PublishedVideo ? 1 : 0 };
        NativeError.Check(NativeMethods.xmax_realtime_set_video_mirror(value, &native));
    }
    public Task<QualityStatus> GetQualityAsync() => operations.Run(GetQuality);
    private static unsafe QualityStatus GetQuality(nint value)
    {
        var native = new xmax_quality_status { struct_size = (uint)sizeof(xmax_quality_status) };
        NativeError.Check(NativeMethods.xmax_realtime_get_quality(value, &native));
        return new(native.adaptive_enabled != 0, native.uplink_level, native.downlink_level,
            VideoFormat.From(native.uplink), VideoFormat.From(native.downlink));
    }
    public Task<RealtimeEvent> GetStateAsync() => operations.Run(GetState);
    private static unsafe RealtimeEvent GetState(nint value)
    {
        var native = new xmax_event_info { struct_size = (uint)sizeof(xmax_event_info) };
        NativeError.Check(NativeMethods.xmax_realtime_get_state_info(value, &native));
        return RealtimeEvent.From(native);
    }
    private async Task PumpEventsAsync()
    {
        try
        {
            while (!eventsStop.IsCancellationRequested)
            {
                foreach (var item in PollEvents())
                    foreach (Action<RealtimeEvent> callback in EventReceived?.GetInvocationList() ?? [])
                        try { callback(item); } catch (Exception error) { NotifyCallbackFault(error); }
                await Task.Delay(25, eventsStop.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (eventsStop.IsCancellationRequested) { }
        catch (Exception error) { NotifyCallbackFault(error); }
    }
    private unsafe List<RealtimeEvent> PollEvents()
    {
        using var lease = handle.Borrow();
        List<RealtimeEvent> batch = [];
        for (int i = 0; i < 256; ++i)
        {
            var native = new xmax_event_info { struct_size = (uint)sizeof(xmax_event_info) };
            int result = NativeMethods.xmax_realtime_poll_event_info(lease.Value, &native);
            NativeError.Check(result);
            if (result == 1) break;
            batch.Add(RealtimeEvent.From(native));
        }
        return batch;
    }
    private void NotifyCallbackFault(Exception error)
    { foreach (Action<Exception> callback in CallbackFaulted?.GetInvocationList() ?? []) try { callback(error); } catch { } }
    public ValueTask DisposeAsync() => new(operations.DisposeAsync(
        value => NativeError.Check(NativeMethods.xmax_realtime_close(value)), async () =>
        {
            eventsStop.Cancel();
            await eventsWorker.ConfigureAwait(false);
            eventsStop.Dispose();
        }));

    private sealed class VideoSource(NativeHandle handle, bool local) : IXmaxVideoSource
    {
        public unsafe XmaxVideoFrame? TryAcquireFrame()
        {
            if (handle.IsClosed) return null;
            using var lease = handle.Borrow();
            nint frame = 0;
            int result = local ? NativeMethods.xmax_realtime_acquire_local_frame(lease.Value, &frame)
                : NativeMethods.xmax_realtime_acquire_frame(lease.Value, &frame);
            NativeError.Check(result);
            return result == 1 || frame == 0 ? null : new(new(frame, HandleKind.Frame));
        }
    }
}
