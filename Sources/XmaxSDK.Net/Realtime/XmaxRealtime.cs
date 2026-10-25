//
//  XmaxRealtime.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;

/// <summary>
/// An owner of local media, a realtime connection, and generation conditions.
/// </summary>
/// <remarks>
/// Await asynchronous operations instead of blocking the UI thread. Native failures are reported as XmaxException.
/// DisposeAsync rejects new work, closes native resources, and drains in-flight calls and events.
/// Do not synchronously wait for disposal inside an SDK event or media callback.
/// </remarks>
public sealed partial class XmaxRealtime : IAsyncDisposable
{
    private readonly NativeHandle handle;

    private readonly OperationTracker operations;

    private readonly CancellationTokenSource eventsStop = new();

    private readonly Task eventsWorker;

    /// <summary>
    /// Gets the single-consumer latest-frame mailbox for local input.
    /// </summary>
    public IXmaxVideoSource LocalVideo { get; }

    /// <summary>
    /// Gets the single-consumer latest-frame mailbox for generated result video.
    /// </summary>
    public IXmaxVideoSource RemoteVideo { get; }

    /// <summary>
    /// Occurs serially on the background event worker when a native event is consumed.
    /// </summary>
    /// <remarks>
    /// Dispatch UI updates to your UI thread. Handler exceptions are forwarded to CallbackFaulted without stopping event delivery.
    /// </remarks>
    public event Action<RealtimeEvent>? EventReceived;

    /// <summary>
    /// Occurs when an event handler, remote media listener, or event worker reports an exception.
    /// </summary>
    /// <remarks>
    /// The callback runs on the thread reporting the failure. Exceptions thrown by this handler are ignored; dispatch UI work explicitly.
    /// </remarks>
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

    /// <summary>
    /// Prepares local camera video and waits for its first real frame without creating a cloud session.
    /// </summary>
    /// <param name="deviceId">The enumerated device identifier; null or empty selects the system default camera.</param>
    /// <param name="format">The camera encoding target; an all-zero format selects the model default. Actual capture dimensions may differ.</param>
    /// <param name="microphone">Whether to request microphone capture when a connection is subsequently established.</param>
    /// <param name="cancellationToken">Cancels startup by closing the controller connection and local media.</param>
    /// <returns>A task completing when camera media is ready.</returns>
    /// <remarks>
    /// Prepare media before connecting. Capture frames retain device-supported dimensions; the RTC encoder handles the encoding target.
    /// </remarks>
    public Task PrepareCameraAsync(
        string? deviceId = null,
        VideoFormat format = default,
        bool microphone = false,
        CancellationToken cancellationToken = default
    ) => operations.Run(value =>
        NativeCancellation.Run(cancellationToken, () => { PrepareCamera(value, deviceId, format, microphone); return 0; },
            () => NativeMethods.xmax_realtime_close(value)), cancellationToken);

    private static unsafe void PrepareCamera(nint handle, string? device, VideoFormat format, bool microphone)
    {
        using var text = new Utf8(device);
        var config = new xmax_camera_config { struct_size = (uint)sizeof(xmax_camera_config), device_id = text.Pointer,
            format = format.Native, use_microphone = microphone ? 1 : 0 };
        NativeError.Check(NativeMethods.xmax_realtime_prepare_camera(handle, &config));
    }

    /// <summary>
    /// Prepares disconnected video input supplied by your app, replacing existing local media.
    /// </summary>
    /// <param name="format">The fixed external video dimensions and nominal frame rate.</param>
    /// <returns>A task completing when the external binding is ready.</returns>
    /// <remarks>
    /// Prefer BindExternalAsync when a source-bound submission receiver is needed. Binding must occur before connecting.
    /// </remarks>
    public Task PrepareExternalAsync(
        VideoFormat format
    ) => Run(value => PrepareExternal(value, format));

    private static unsafe void PrepareExternal(nint value, VideoFormat format)
    { var native = format.Native; NativeError.Check(NativeMethods.xmax_realtime_prepare_external(value, &native)); }

    /// <summary>
    /// Creates a server session and joins RTC without starting generation.
    /// </summary>
    /// <param name="microphone">Whether to request microphone capture; the camera preparation option also applies.</param>
    /// <param name="cancellationToken">Cancels by disconnecting the whole connection, not an individual generation task.</param>
    /// <returns>A task completing when the connection is ready.</returns>
    /// <remarks>
    /// Prepare local media first.
    /// </remarks>
    public Task ConnectAsync(
        bool microphone = false,
        CancellationToken cancellationToken = default
    ) =>
        Run(value => NativeError.Check(NativeMethods.xmax_realtime_connect(value, microphone ? 1 : 0)), cancellationToken, true);

    /// <summary>
    /// Connects as needed, starts generation, and waits for the first result frame.
    /// </summary>
    /// <param name="context">The generation prompt, reference image, and face-restoration conditions.</param>
    /// <param name="microphone">Whether to request microphone capture when connecting.</param>
    /// <param name="cancellationToken">Cancels by disconnecting the whole connection; cleanup finishes before the task completes.</param>
    /// <returns>A task completing when the first result frame is committed.</returns>
    /// <remarks>
    /// Prepare media first. For external input, continue submitting frames on another execution path while awaiting this task.
    /// A newer generation request cancels an unfinished request; only successful requests replace cached conditions.
    /// </remarks>
    public Task StartAsync(
        GenerationContext context,
        bool microphone = false,
        CancellationToken cancellationToken = default
    ) =>
        Run(value => Generate(value, context, 0, microphone), cancellationToken, true);

    /// <summary>
    /// Starts or updates generation with supplied conditions, or reuses the last successful conditions.
    /// </summary>
    /// <param name="context">The new generation conditions; null reuses cached conditions and requires a prior successful request.</param>
    /// <param name="cancellationToken">Cancels by disconnecting the whole connection; cleanup finishes before the task completes.</param>
    /// <returns>A task completing when the native generation operation succeeds.</returns>
    /// <remarks>
    /// Ready media connects as needed. Continue supplying external frames during startup.
    /// A newer generation request cancels an unfinished request; only successful requests replace cached conditions.
    /// </remarks>
    public Task GenerateAsync(
        GenerationContext? context = null,
        CancellationToken cancellationToken = default
    ) =>
        Run(value => Generate(value, context, 1, false), cancellationToken, true);

    /// <summary>
    /// Replaces conditions for the currently running generation task.
    /// </summary>
    /// <param name="context">The generation prompt, reference image, and face-restoration conditions.</param>
    /// <param name="cancellationToken">Cancels by disconnecting the whole connection; cleanup finishes before the task completes.</param>
    /// <returns>A task completing when the native update succeeds.</returns>
    /// <remarks>
    /// Generation must already be active. A newer generation request cancels an unfinished update; failed requests do not replace cached conditions.
    /// </remarks>
    public Task UpdateAsync(
        GenerationContext context,
        CancellationToken cancellationToken = default
    ) =>
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

    /// <summary>
    /// Switches camera capture while retaining the connection and restoring generation conditions.
    /// </summary>
    /// <param name="deviceId">The enumerated camera identifier; an empty string selects the default device.</param>
    /// <returns>A task completing when capture and any generation restart finish.</returns>
    /// <remarks>
    /// The operation waits for the new camera. Failure releases the connection and local media.
    /// </remarks>
    public Task SwitchCameraAsync(
        string deviceId
    ) => Run(value => SwitchCamera(value, deviceId));

    private static unsafe void SwitchCamera(nint value, string deviceId)
    { using var text = new Utf8(deviceId); NativeError.Check(NativeMethods.xmax_realtime_switch_camera(value, text.Pointer)); }

    /// <summary>
    /// Stops local capture and removes input bindings after disconnection.
    /// </summary>
    /// <returns>A task completing when local media is stopped.</returns>
    /// <remarks>
    /// Disconnect an active connection before calling this method.
    /// </remarks>
    public Task StopMediaAsync() => Run(value => NativeError.Check(NativeMethods.xmax_realtime_stop_media(value)));

    /// <summary>
    /// Disconnects the server session and RTC while retaining local preview and successful generation conditions.
    /// </summary>
    /// <returns>A task completing after disconnection.</returns>
    /// <remarks>
    /// Requests cancellation of in-flight lifecycle work. Prepared local media can be used for reconnection.
    /// </remarks>
    public Task DisconnectAsync() => Run(value => NativeError.Check(NativeMethods.xmax_realtime_disconnect(value)));

    /// <summary>
    /// Disconnects and releases local media and cached generation conditions.
    /// </summary>
    /// <returns>A task completing after native resources are closed.</returns>
    /// <remarks>
    /// The controller remains usable: prepare media again before starting a new session. This method requests cancellation of in-flight startup.
    /// </remarks>
    public Task CloseAsync() => Run(value => NativeError.Check(NativeMethods.xmax_realtime_close(value)));

    /// <summary>
    /// Sets remote playback volume for the current or next connection.
    /// </summary>
    /// <param name="volume">A finite volume from 0 through 1; 0 mutes playback.</param>
    /// <returns>A task completing after the playback setting is applied.</returns>
    /// <remarks>
    /// Playback starts at 0. This control does not wait for native connection or generation admission and does not change recorded PCM.
    /// </remarks>
    public Task SetRemoteVolumeAsync(
        float volume
    ) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_remote_volume(value, volume)));

    /// <summary>
    /// Enables or disables independent uplink and downlink quality adaptation.
    /// </summary>
    /// <param name="enabled">Whether automatic network-based adaptation is enabled.</param>
    /// <returns>A task completing after the policy changes.</returns>
    /// <remarks>
    /// Changing the policy resets the network observation window.
    /// </remarks>
    public Task SetAdaptiveQualityAsync(
        bool enabled
    ) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_adaptive_quality(value, enabled ? 1 : 0)));

    /// <summary>
    /// Sets the uplink quality level during generation.
    /// </summary>
    /// <param name="level">The level from 1 for highest quality through 5 for lowest bandwidth use.</param>
    /// <returns>A task completing after the level is applied.</returns>
    /// <remarks>
    /// Manual changes retain automatic network adaptation. A failed change preserves the previous level.
    /// </remarks>
    public Task SetUplinkQualityLevelAsync(
        int level
    ) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_uplink_quality_level(value, level)));

    /// <summary>
    /// Sets the downlink quality level during generation.
    /// </summary>
    /// <param name="level">The level from 1 for highest quality through 5 for lowest bandwidth use.</param>
    /// <returns>A task completing after the level is applied.</returns>
    /// <remarks>
    /// Manual changes retain automatic network adaptation. A failed change preserves the previous level.
    /// </remarks>
    public Task SetDownlinkQualityLevelAsync(
        int level
    ) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_downlink_quality_level(value, level)));

    /// <summary>
    /// Changes a ready, disconnected camera encoding target while retaining the device and preview binding.
    /// </summary>
    /// <param name="format">The model-supported target dimensions and nominal frame rate.</param>
    /// <returns>A task completing after reconfiguration or recovery.</returns>
    /// <remarks>
    /// Existing bitrate preferences are retained. Preparation failure attempts to restore the previous format and reports the original error.
    /// </remarks>
    public Task UpdateCameraFormatAsync(
        VideoFormat format
    ) => Run(value => UpdateCameraFormat(value, format));

    private static unsafe void UpdateCameraFormat(nint value, VideoFormat format)
    { var native = format.Native; NativeError.Check(NativeMethods.xmax_realtime_update_camera_format(value, &native)); }

    /// <summary>
    /// Sets RTC bitrate limits and encoding preference.
    /// </summary>
    /// <param name="config">The encoder limits and adaptation tradeoff.</param>
    /// <returns>A task completing after the encoding settings are applied.</returns>
    public Task SetEncodingAsync(
        EncodingConfiguration config
    ) => Run(value => SetEncoding(value, config));

    private static unsafe void SetEncoding(nint value, EncodingConfiguration config)
    {
        var native = new xmax_encoding_config { struct_size = (uint)sizeof(xmax_encoding_config),
            minimum_bitrate_kbps = config.MinimumBitrateKbps, maximum_bitrate_kbps = config.MaximumBitrateKbps, preference = (int)config.Preference };
        NativeError.Check(NativeMethods.xmax_realtime_set_encoding(value, &native));
    }

    /// <summary>
    /// Sets independent local-display, result-display, and publication mirroring.
    /// </summary>
    /// <param name="config">The three mirror choices; display choices affect frame metadata without rewriting pixels.</param>
    /// <returns>A task completing after the mirror configuration is applied.</returns>
    /// <remarks>
    /// Settings survive disconnection. Apply display mirror metadata once when using a custom renderer; SDK video controls apply it automatically.
    /// </remarks>
    public Task SetVideoMirrorAsync(
        VideoMirrorConfiguration config
    ) => Run(value => SetMirror(value, config));

    private static unsafe void SetMirror(nint value, VideoMirrorConfiguration config)
    {
        var native = new xmax_video_mirror_config { struct_size = (uint)sizeof(xmax_video_mirror_config),
            local_preview = config.LocalPreview ? 1 : 0, remote_view = config.RemoteView ? 1 : 0, published_video = config.PublishedVideo ? 1 : 0 };
        NativeError.Check(NativeMethods.xmax_realtime_set_video_mirror(value, &native));
    }

    /// <summary>
    /// Reads the effective directional quality settings without consuming events.
    /// </summary>
    /// <returns>A task containing the latest quality snapshot.</returns>
    /// <remarks>
    /// This query may wait for native lifecycle work.
    /// </remarks>
    public Task<QualityStatus> GetQualityAsync() => operations.Run(GetQuality);

    private static unsafe QualityStatus GetQuality(nint value)
    {
        var native = new xmax_quality_status { struct_size = (uint)sizeof(xmax_quality_status) };
        NativeError.Check(NativeMethods.xmax_realtime_get_quality(value, &native));
        return new(native.adaptive_enabled != 0, native.uplink_level, native.downlink_level,
            VideoFormat.From(native.uplink), VideoFormat.From(native.downlink));
    }

    /// <summary>
    /// Reads lifecycle state, identifiers, state reason, and associated error without consuming events.
    /// </summary>
    /// <returns>A task containing the state snapshot.</returns>
    /// <remarks>
    /// This query may wait for native lifecycle work.
    /// </remarks>
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

    /// <summary>
    /// Closes this controller, drains in-flight operations and events, and releases its native handle.
    /// </summary>
    /// <returns>An awaitable operation completing after cleanup.</returns>
    /// <remarks>
    /// New operations are rejected once disposal starts. Acquired frame owners remain independently valid.
    /// Do not synchronously wait for this operation inside an SDK event or media callback.
    /// </remarks>
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
