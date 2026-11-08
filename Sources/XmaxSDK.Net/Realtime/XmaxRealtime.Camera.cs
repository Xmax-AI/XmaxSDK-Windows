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
    /// <summary>Creates camera media with initial mirroring and microphone selection, without a cloud session.</summary>
    /// <param name="videoFormat">The camera output format. An omitted value selects the model default.</param>
    /// <param name="camera">The camera selection; null uses the system default.</param>
    /// <param name="microphone">The initial recording input; null disables the microphone.</param>
    /// <param name="mirror">Mirrors local preview and published camera video, preserving result-display settings.</param>
    /// <param name="cancellationToken">Cancels camera preparation and closes local media.</param>
    /// <returns>The local media stream, available to bind once native capture starts.</returns>
    /// <remarks>The stream can connect immediately. Readiness requires an attached preview and the first usable frame.
    /// Enabled microphones are checked during creation.</remarks>
    public Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        VideoFormat videoFormat = default,
        RealtimeCamera? camera = null,
        RealtimeMicrophone? microphone = null,
        bool mirror = false,
        CancellationToken cancellationToken = default
    )
    {
        string? deviceId = camera?.DeviceId;
        microphone ??= RealtimeMicrophone.Disabled;
        return ChangeLocalMediaAsync(() => operations.Run(value => NativeCancellation.Run(
            cancellationToken,
            () => {
                CreateCamera(value, videoFormat, deviceId, mirror, microphone);
                return CreateLocalStream(value, cameraPreview: true, cameraDeviceId: deviceId);
            },
            () => NativeMethods.xmax_realtime_close(value)
        ), cancellationToken), cancellationToken);
    }

    /// <summary>Creates camera media using a resolution preset and its model-defined frame rate.</summary>
    /// <param name="preset">The X2.1 camera resolution preset.</param>
    /// <param name="camera">The camera selection; null uses the system default.</param>
    /// <param name="microphone">The initial microphone selection; null disables capture.</param>
    /// <param name="mirror">Mirrors local preview and publication together.</param>
    /// <param name="cancellationToken">Cancels preparation and releases local media.</param>
    /// <returns>The prepared local camera stream.</returns>
    public Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        RealtimeCameraPreset preset,
        RealtimeCamera? camera = null,
        RealtimeMicrophone? microphone = null,
        bool mirror = false,
        CancellationToken cancellationToken = default
    ) => CreateLocalCameraStreamAsync(
        CameraPresetFormat(preset),
        camera,
        microphone,
        mirror,
        cancellationToken
    );

    private VideoFormat CameraPresetFormat(RealtimeCameraPreset preset)
    {
        var size = preset switch
        {
            RealtimeCameraPreset.Resolution1920x1080 => (1920u, 1080u),
            RealtimeCameraPreset.Resolution1080x1920 => (1080u, 1920u),
            RealtimeCameraPreset.Resolution1408x1408 => (1408u, 1408u),
            RealtimeCameraPreset.Resolution1152x1536 => (1152u, 1536u),
            RealtimeCameraPreset.Resolution1536x1152 => (1536u, 1152u),
            _ => throw new ArgumentOutOfRangeException(nameof(preset))
        };
        foreach (var format in XmaxClient.GetModelInfo(Configuration.Model).SupportedFormats)
            if ((format.Width, format.Height) == size)
                return format;
        throw new ArgumentException("The model does not support the camera preset.", nameof(preset));
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
        VideoFormat format
    ) => ChangeLocalMediaAsync(() => Run(value => UpdateCameraFormat(value, format)));

    private unsafe void UpdateCameraFormat(nint value, VideoFormat format)
    {
        var native = format.Native;
        var encoding = format.NativeEncoding;
        NativeError.Check(NativeMethods.xmax_realtime_update_camera_format_with_encoding(value, &native, &encoding));
        RefreshLocalTrackFormat(value);
    }

    /// <summary>Changes camera dimensions while preserving the current frame rate and encoding options.</summary>
    public Task UpdateCameraFormatAsync(RealtimeCameraPreset preset) => ChangeLocalMediaAsync(() => Run(value =>
    {
        if (!Enum.IsDefined(preset))
            throw new ArgumentOutOfRangeException(nameof(preset));
        // The native enum reserves zero for the absence of a camera preset.
        NativeError.Check(NativeMethods.xmax_realtime_update_camera_preset(value, (int)preset + 1));
        RefreshLocalTrackFormat(value);
    }));

    /// <summary>Stops local camera capture after disconnection.</summary>
    public Task StopLocalCameraStreamAsync() => ChangeLocalMediaAsync(
        () => Run(value => NativeError.Check(NativeMethods.xmax_realtime_stop_local_camera_stream(value)))
    );

    /// <summary>Reads the active camera format, or null when no camera is active.</summary>
    public Task<VideoFormat?> GetCurrentCameraVideoFormatAsync() => operations.Run<VideoFormat?>(ReadCameraFormat);

    /// <summary>Reads the resolution preset of the active camera.</summary>
    public async Task<RealtimeCameraPreset?> GetCurrentCameraPresetAsync()
    {
        var format = await GetCurrentCameraVideoFormatAsync().ConfigureAwait(false);
        return (format?.Width, format?.Height) switch
        {
            (1920, 1080) => RealtimeCameraPreset.Resolution1920x1080,
            (1080, 1920) => RealtimeCameraPreset.Resolution1080x1920,
            (1408, 1408) => RealtimeCameraPreset.Resolution1408x1408,
            (1152, 1536) => RealtimeCameraPreset.Resolution1152x1536,
            (1536, 1152) => RealtimeCameraPreset.Resolution1536x1152,
            _ => null
        };
    }

    private static unsafe VideoFormat? ReadCameraFormat(nint value)
    {
        xmax_video_format format;
        int preset;
        var result = NativeMethods.xmax_realtime_get_camera_format(value, &format, &preset);
        NativeError.Check(result);
        return result == 1 || format.width == 0 ? null : VideoFormat.From(format).WithEncoding(ReadEncoding(value));
    }

    private static unsafe void CreateCamera(
        nint realtime,
        VideoFormat format,
        string? deviceId,
        bool mirror,
        RealtimeMicrophone microphone
    )
    {
        using var camera = new Utf8(deviceId);
        using var recording = new Utf8(microphone.DeviceId);
        var options = new xmax_local_camera_options {
            struct_size = (uint)sizeof(xmax_local_camera_options),
            device_id = camera.Pointer,
            video_format = format.Native,
            mirror = mirror ? 1 : 0,
            microphone = !microphone.IsEnabled ? 0 : microphone.DeviceId is null ? 1 : 2,
            microphone_device_id = recording.Pointer
        };
        var encoding = format.NativeEncoding;
        NativeError.Check(NativeMethods.xmax_realtime_create_local_camera_stream_with_encoding(realtime, &options, &encoding));
    }
}
