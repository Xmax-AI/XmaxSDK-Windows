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
    /// Gets the single-consumer latest-frame mailbox for generated result video after SDK beauty.
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

    /// <summary>Gets the configuration supplied when creating this controller.</summary>
    public RealtimeConfiguration Configuration { get; }

    /// <summary>Gets the initial realtime configuration.</summary>
    public RealtimeConfiguration Options => Configuration;

    internal XmaxRealtime(NativeHandle handle, RealtimeConfiguration? options = null)
    {
        this.handle = handle;
        Configuration = options ?? new();
        operations = new(handle);
        LocalVideo = new VideoSource(handle, true);
        RemoteVideo = new VideoSource(handle, false);
        stateListener.Publish(new(RealtimeConnectionState.Idle));
        eventsWorker = Task.Run(PumpEventsAsync);
    }

    private Task Run(Action<nint> action) => operations.Run(value =>
    {
        action(value);
        return 0;
    });

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
    ) => ChangeLocalMediaAsync(() => Run(value => PrepareExternal(value, format)));

    private static unsafe void PrepareExternal(nint value, VideoFormat format)
    { var native = format.Native; NativeError.Check(NativeMethods.xmax_realtime_prepare_external(value, &native)); }

    /// <summary>
    /// Switches camera capture while retaining the connection and restoring generation conditions.
    /// </summary>
    /// <param name="deviceId">The enumerated camera identifier; an empty string selects the default device.</param>
    /// <returns>A task completing when capture and any generation restart finish.</returns>
    /// <remarks>
    /// The operation waits for the new camera and restarts any active generation. If the camera fails,
    /// the SDK attempts to restore local capture. A failure during generation closes the connection.
    /// </remarks>
    public Task<RealtimeMediaStream> SwitchCameraAsync(
        string deviceId
    ) => ChangeLocalMediaAsync(() => operations.Run(value =>
    {
        SwitchCamera(value, deviceId);
        return CreateLocalStream(value, cameraDeviceId: deviceId, isCamera: true);
    }));

    private static unsafe void SwitchCamera(nint value, string deviceId)
    { using var text = new Utf8(deviceId); NativeError.Check(NativeMethods.xmax_realtime_switch_camera(value, text.Pointer)); }

    /// <summary>
    /// Stops local capture and removes input bindings after disconnection.
    /// </summary>
    /// <returns>A task completing when local media is stopped.</returns>
    /// <remarks>
    /// Disconnect an active connection before calling this method.
    /// </remarks>
    public Task StopMediaAsync() => ChangeLocalMediaAsync(
        () => Run(value => NativeError.Check(NativeMethods.xmax_realtime_stop_media(value)))
    );

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
    /// Applies a uplink quality target during generation.
    /// </summary>
    /// <param name="level">The transport quality target.</param>
    /// <returns>A task completing when the target has been applied.</returns>
    public Task SetUplinkQualityLevelAsync(
        RealtimeQualityLevel level
    ) => SetUplinkQualityLevelAsync((int)level);

    /// <summary>
    /// Reads the applied uplink quality level.
    /// </summary>
    /// <returns>The level, or null while the RTC connection is inactive.</returns>
    public Task<RealtimeQualityLevel?> GetUplinkQualityLevelAsync() => operations.Run(value =>
    {
        int level = GetQuality(value).UplinkLevel;
        return level == 0 ? (RealtimeQualityLevel?)null : (RealtimeQualityLevel)level;
    });

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
    ) => Run(value =>
    {
        NativeError.Check(NativeMethods.xmax_realtime_set_downlink_quality_level(value, level));
        RefreshResultTrackFormat(value);
    });

    /// <summary>
    /// Applies a downlink quality target during generation.
    /// </summary>
    /// <param name="level">The transport quality target.</param>
    /// <returns>A task completing when the target has been applied.</returns>
    public Task SetDownlinkQualityLevelAsync(
        RealtimeQualityLevel level
    ) => SetDownlinkQualityLevelAsync((int)level);

    /// <summary>
    /// Reads the applied downlink quality level.
    /// </summary>
    /// <returns>The level, or null while the RTC connection is inactive.</returns>
    public Task<RealtimeQualityLevel?> GetDownlinkQualityLevelAsync() => operations.Run(value =>
    {
        int level = GetQuality(value).DownlinkLevel;
        return level == 0 ? (RealtimeQualityLevel?)null : (RealtimeQualityLevel)level;
    });

    /// <summary>
    /// Changes a ready, disconnected camera encoding target while retaining the device and preview binding.
    /// </summary>
    /// <param name="format">The model-supported target dimensions and nominal frame rate.</param>
    /// <returns>A task completing after reconfiguration or recovery.</returns>
    /// <remarks>
    /// Preparation failure attempts to restore the previous format and reports the original error.
    /// </remarks>
    public Task UpdateCameraFormatAsync(
        VideoFormat format
    ) => ChangeLocalMediaAsync(() => Run(value => UpdateCameraFormat(value, format)));

    private unsafe void UpdateCameraFormat(nint value, VideoFormat format)
    {
        var native = format.Native;
        var encoding = format.NativeEncoding;
        NativeError.Check(NativeMethods.xmax_realtime_update_camera_format_with_encoding(value, &native, &encoding));
        RefreshLocalTrackFormat(value);
    }

    /// <summary>
    /// Sets RTC bitrate limits and encoding preference.
    /// </summary>
    /// <param name="config">The encoder limits and adaptation tradeoff.</param>
    /// <returns>A task completing after the encoding settings are applied.</returns>
    public Task SetEncodingAsync(
        EncodingConfiguration config
    ) => Run(value => SetEncoding(value, config));

    private unsafe void SetEncoding(nint value, EncodingConfiguration config)
    {
        var native = new xmax_encoding_config { struct_size = (uint)sizeof(xmax_encoding_config),
            minimum_bitrate_kbps = config.MinimumBitrateKbps, maximum_bitrate_kbps = config.MaximumBitrateKbps, preference = (int)config.Preference };
        NativeError.Check(NativeMethods.xmax_realtime_set_encoding(value, &native));
        RefreshLocalTrackFormat(value);
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
    /// The snapshot is available while connection, generation, or cleanup is in progress.
    /// </remarks>
    public Task<RealtimeState> GetStateAsync() => operations.Run(GetState);

    private static unsafe RealtimeState GetState(nint value)
    {
        var native = new xmax_event_info { struct_size = (uint)sizeof(xmax_event_info) };
        NativeError.Check(NativeMethods.xmax_realtime_get_state_info(value, &native));
        return RealtimeState.From(RealtimeEvent.From(native));
    }

    private async Task PumpEventsAsync()
    {
        try
        {
            while (!eventsStop.IsCancellationRequested)
            {
                foreach (var item in PollEvents())
                {
                    if (item.State == RealtimeConnectionState.Idle && Volatile.Read(ref externalAttachment) is not null)
                        _ = ReconcileExternalSourceAsync();
                    try { DispatchTypedEvent(item); } catch (Exception error) { NotifyCallbackFault(error); }
                    foreach (Action<RealtimeEvent> callback in EventReceived?.GetInvocationList() ?? [])
                        try { callback(item); } catch (Exception error) { NotifyCallbackFault(error); }
                }
                ApplyStateRegistrations();
                await Task.Delay(25, eventsStop.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (eventsStop.IsCancellationRequested) { }
        catch (Exception error) { NotifyCallbackFault(error); }
        finally { CloseStateRegistrations(); }
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
    public ValueTask DisposeAsync()
    {
        BeginExternalClose(disposing: true);
        return new(operations.DisposeAsync(CloseNative, async () =>
        {
            await FinishExternalCloseAsync().ConfigureAwait(false);
            eventsStop.Cancel();
            await eventsWorker.ConfigureAwait(false);
            ClearTypedListeners();
            eventsStop.Dispose();
        }));
    }

    private sealed class VideoSource(NativeHandle handle, bool local, ulong revision = 0) : IXmaxVideoSource
    {
        public unsafe XmaxVideoFrame? TryAcquireFrame()
        {
            if (handle.IsClosed) return null;
            using var lease = handle.Borrow();
            nint frame = 0;
            int result = revision != 0
                ? NativeMethods.xmax_realtime_acquire_stream_frame(lease.Value, local ? 1 : 0, revision, &frame)
                : local ? NativeMethods.xmax_realtime_acquire_local_frame(lease.Value, &frame)
                        : NativeMethods.xmax_realtime_acquire_frame(lease.Value, &frame);
            NativeError.Check(result);
            if (result == 1 || frame == 0)
                return null;

            return new(new NativeHandle(frame, HandleKind.Frame));
        }
    }
}
