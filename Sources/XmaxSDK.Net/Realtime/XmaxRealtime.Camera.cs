//
//  XmaxRealtime.Camera.cs
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

internal sealed partial class XmaxRealtime
{
    /// <summary>Creates camera media without a cloud session, using Web-compatible defaults.</summary>
    /// <param name="videoFormat">The output target after rotation; omission follows the model default.</param>
    /// <param name="camera">The camera selection; omission prefers a front-facing camera.</param>
    /// <param name="microphone">The microphone selected for connection; omission disables capture.</param>
    /// <param name="mirror">An explicit mirror override; null selects automatic front/default-device mirroring.</param>
    /// <param name="captureFormat">Preferred input dimensions and rate before rotation; omission follows the target.</param>
    /// <param name="rotation">Clockwise rotation applied before mirroring, defaulting to zero.</param>
    /// <param name="warmupMs">Finite non-negative warmup at connection time before publication.</param>
    /// <returns>The local stream. Bind its video track to a preview for first-frame readiness.</returns>
    /// <remarks>Creation retains remote playback volume. Capture format is independent of the output target.</remarks>
    public Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        RealtimeVideoFormat videoFormat = default,
        (uint width, uint height, double? fps)? captureFormat = null,
        RealtimeCamera? camera = null,
        RealtimeMicrophone? microphone = null,
        CameraRotation rotation = CameraRotation.None,
        bool? mirror = null,
        double warmupMs = 0)
    {
        if (!Enum.IsDefined(rotation)) throw new ArgumentOutOfRangeException(nameof(rotation));
        if (!double.IsFinite(warmupMs) || warmupMs < 0)
            throw new ArgumentOutOfRangeException(nameof(warmupMs));
        if (captureFormat is { } capture && (capture.width is 0 or > 8192 || capture.height is 0 or > 8192 ||
            capture.fps is { } fps && (!double.IsFinite(fps) || fps <= 0 || fps > int.MaxValue)))
            throw new ArgumentOutOfRangeException(nameof(captureFormat));
        camera ??= RealtimeCamera.Position(CameraPosition.Front);
        microphone ??= RealtimeMicrophone.Disabled;
        return ChangeLocalMediaAsync(async () =>
        {
            using var startup = new CancellationTokenSource();
            lock (externalStartupGate)
            {
                ObjectDisposedException.ThrowIf(disposingLocalMedia, this);
                if (externalCloseCount != 0)
                    throw new OperationCanceledException("The controller is closing local media.");
                cameraStartup = startup;
            }
            try
            {
                return await operations.Run(value => NativeCancellation.Run(
                    startup.Token,
                    () => {
                        CreateCamera(value, videoFormat, camera, microphone, mirror, captureFormat, rotation, warmupMs);
                        return CreateLocalStream(value, cameraPreview: true, cameraDeviceId: camera.DeviceId);
                    },
                    () => NativeMethods.xmax_realtime_close(value)
                ), startup.Token).ConfigureAwait(false);
            }
            finally
            {
                Task? cancelling;
                lock (externalStartupGate)
                {
                    cameraStartup = null;
                    cancelling = cameraStartupCancellation;
                    cameraStartupCancellation = null;
                }
                if (cancelling is not null)
                    await ObserveExternalCancellationAsync(cancelling).ConfigureAwait(false);
            }
        });
    }

    private CancellationTokenSource? cameraStartup;
    private Task? cameraStartupCancellation;

    // Called under externalStartupGate. Close/stop own cancellation; callers need no token.
    private Task CancelCameraStartup() => cameraStartup is null ? Task.CompletedTask
        : cameraStartupCancellation ??= cameraStartup.CancelAsync();

    /// <summary>Creates camera media using a model-supported preset and the same optional capture parameters.</summary>
    public Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        RealtimeCameraFormat videoFormat,
        (uint width, uint height, double? fps)? captureFormat = null,
        RealtimeCamera? camera = null,
        RealtimeMicrophone? microphone = null,
        CameraRotation rotation = CameraRotation.None,
        bool? mirror = null,
        double warmupMs = 0) =>
        CreateLocalCameraStreamAsync(ResolveCameraFormat(videoFormat), captureFormat, camera, microphone,
            rotation, mirror, warmupMs);

    private RealtimeVideoFormat ResolveCameraFormat(RealtimeCameraFormat videoFormat)
    {
        var size = videoFormat switch
        {
            RealtimeCameraFormat.Resolution1920x1080 => (1920u, 1080u),
            RealtimeCameraFormat.Resolution1080x1920 => (1080u, 1920u),
            RealtimeCameraFormat.Resolution1408x1408 => (1408u, 1408u),
            RealtimeCameraFormat.Resolution1152x1536 => (1152u, 1536u),
            RealtimeCameraFormat.Resolution1536x1152 => (1536u, 1152u),
            RealtimeCameraFormat.Resolution704x1280 => (704u, 1280u),
            RealtimeCameraFormat.Resolution1280x704 => (1280u, 704u),
            RealtimeCameraFormat.Resolution960x960 => (960u, 960u),
            RealtimeCameraFormat.Resolution1024x768 => (1024u, 768u),
            RealtimeCameraFormat.Resolution768x1024 => (768u, 1024u),
            _ => throw new ArgumentOutOfRangeException(nameof(videoFormat))
        };
        foreach (var format in XmaxClient.GetModelInfo(Configuration.Model).SupportedFormats)
            if ((format.Width, format.Height) == size)
                return format;
        throw new ArgumentException("The model does not support the camera format.", nameof(videoFormat));
    }

    internal Task CompleteLocalCameraPreviewAsync(ulong revision) =>
        Run(value => NativeError.Check(NativeMethods.xmax_realtime_complete_camera_preview(value, revision)));

    /// <summary>Lists the available camera devices without starting capture.</summary>
    public Task<IReadOnlyList<CameraDevice>> ListCameraDevicesAsync(
        CancellationToken cancellationToken = default
    ) => operations.Run(_ => MediaDevices.ReadCameraSnapshot(), cancellationToken);

    /// <summary>
    /// Switches camera capture while retaining the connection and restoring generation conditions.
    /// </summary>
    /// <param name="deviceId">The identifier of an enumerated camera.</param>
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
    /// Changes a ready, disconnected camera encoding target while retaining the device and preview binding.
    /// </summary>
    /// <param name="format">The model-supported target dimensions and nominal frame rate.</param>
    /// <returns>A task completing after reconfiguration or recovery.</returns>
    /// <remarks>
    /// Preparation failure attempts to restore the previous format and reports the original error.
    /// </remarks>
    public Task UpdateCameraFormatAsync(
        RealtimeVideoFormat format
    ) => ChangeLocalMediaAsync(() => Run(value => UpdateCameraFormat(value, format)));

    private unsafe void UpdateCameraFormat(nint value, RealtimeVideoFormat format)
    {
        var native = format.Native;
        var encoding = format.NativeEncoding;
        NativeError.Check(NativeMethods.xmax_realtime_update_camera_format_with_encoding(value, &native, &encoding));
        RefreshLocalTrackFormat(value);
    }

    /// <summary>Changes camera dimensions while preserving the current frame rate and encoding options.</summary>
    public Task UpdateCameraFormatAsync(RealtimeCameraFormat preset) => ChangeLocalMediaAsync(() => Run(value =>
    {
        if (!Enum.IsDefined(preset))
            throw new ArgumentOutOfRangeException(nameof(preset));
        // The native enum reserves zero for the absence of a camera preset.
        NativeError.Check(NativeMethods.xmax_realtime_update_camera_preset(value, (int)preset + 1));
        RefreshLocalTrackFormat(value);
    }));

    /// <summary>Stops local camera capture after disconnection.</summary>
    public Task StopLocalCameraStreamAsync()
    {
        lock (externalStartupGate) _ = CancelCameraStartup();
        return ChangeLocalMediaAsync(
            () => Run(value => NativeError.Check(NativeMethods.xmax_realtime_stop_local_camera_stream(value)))
        );
    }

    /// <summary>Reads the active camera format, or null when no camera is active.</summary>
    public Task<RealtimeVideoFormat?> GetCurrentCameraVideoFormatAsync() => operations.Run<RealtimeVideoFormat?>(ReadCameraFormat);

    /// <summary>Reads the resolution preset of the active camera.</summary>
    public Task<RealtimeCameraFormat?> GetCurrentCameraFormatAsync() =>
        operations.Run<RealtimeCameraFormat?>(ReadCameraPreset);

    private static unsafe RealtimeCameraFormat? ReadCameraPreset(nint value)
    {
        xmax_video_format format;
        int nativePreset;
        NativeError.Check(NativeMethods.xmax_realtime_get_camera_format(value, &format, &nativePreset));
        // The native enum reserves zero for the absence of a camera preset.
        var preset = (RealtimeCameraFormat)(nativePreset - 1);
        return Enum.IsDefined(preset) ? preset : null;
    }

    private static unsafe RealtimeVideoFormat? ReadCameraFormat(nint value)
    {
        xmax_video_format format;
        int preset;
        var result = NativeMethods.xmax_realtime_get_camera_format(value, &format, &preset);
        NativeError.Check(result);
        return result == 1 || format.width == 0 ? null : RealtimeVideoFormat.From(format).WithEncoding(ReadEncoding(value));
    }

    private static unsafe void CreateCamera(
        nint realtime, RealtimeVideoFormat format, RealtimeCamera selection, RealtimeMicrophone microphone,
        bool? mirror, (uint width, uint height, double? fps)? captureFormat, CameraRotation rotation, double warmupMs)
    {
        using var camera = new Utf8(selection.DeviceId);
        using var recording = new Utf8(microphone.DeviceId);
        var options = new xmax_camera_stream_options {
            struct_size = (uint)sizeof(xmax_camera_stream_options),
            device_id = camera.Pointer,
            camera_position = (int)(selection.PreferredPosition ?? CameraPosition.Unspecified),
            video_format = format.Native,
            capture_width = captureFormat?.width ?? 0,
            capture_height = captureFormat?.height ?? 0,
            capture_fps = captureFormat?.fps ?? 0,
            rotation = (int)rotation,
            mirror = mirror is { } mirrored ? mirrored ? 1 : 0 : -1,
            microphone = !microphone.IsEnabled ? 0 : microphone.DeviceId is null ? 1 : 2,
            microphone_device_id = recording.Pointer,
            warmup_ms = warmupMs,
            encoding = format.NativeEncoding
        };
        NativeError.Check(NativeMethods.xmax_realtime_create_local_camera_stream_with_options(realtime, &options));
    }
}
