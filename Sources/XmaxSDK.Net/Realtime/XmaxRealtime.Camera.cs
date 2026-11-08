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

public sealed partial class XmaxRealtime
{
    /// <summary>Creates camera media with initial mirroring and microphone selection, without a cloud session.</summary>
    /// <param name="videoFormat">The camera output format. An omitted value selects the model default.</param>
    /// <param name="deviceId">The camera identifier; null selects the system default.</param>
    /// <param name="mirror">Mirrors local preview and published camera video, preserving result-display settings.</param>
    /// <param name="microphone">The initial recording input; null disables the microphone.</param>
    /// <param name="cancellationToken">Cancels camera preparation and closes local media.</param>
    /// <returns>The local media stream, ready to bind to a video view after the first captured frame.</returns>
    /// <remarks>The stream can connect immediately. Attaching a preview can report readiness before connection.
    /// Enabled microphones are checked during creation.</remarks>
    public Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        VideoFormat videoFormat = default,
        string? deviceId = null,
        bool mirror = false,
        RealtimeMicrophone? microphone = null,
        CancellationToken cancellationToken = default
    )
    {
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

    /// <summary>Creates camera media using a model-supported resolution preset at 30 fps.</summary>
    /// <param name="preset">The X2.1 camera resolution preset.</param>
    /// <param name="deviceId">The camera identifier; null selects the default camera.</param>
    /// <param name="mirror">Mirrors local preview and publication together.</param>
    /// <param name="microphone">The initial microphone selection; null disables capture.</param>
    /// <param name="cancellationToken">Cancels preparation and releases local media.</param>
    /// <returns>The prepared local camera stream.</returns>
    public Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        RealtimeCameraPreset preset,
        string? deviceId = null,
        bool mirror = false,
        RealtimeMicrophone? microphone = null,
        CancellationToken cancellationToken = default
    ) => CreateLocalCameraStreamAsync(
        CameraPresetFormat(preset),
        deviceId,
        mirror,
        microphone,
        cancellationToken
    );

    private static VideoFormat CameraPresetFormat(RealtimeCameraPreset preset) => preset switch {
            RealtimeCameraPreset.Resolution1920x1080 => new VideoFormat(1920, 1080),
            RealtimeCameraPreset.Resolution1080x1920 => new VideoFormat(1080, 1920),
            RealtimeCameraPreset.Resolution1408x1408 => new VideoFormat(1408, 1408),
            RealtimeCameraPreset.Resolution1152x1536 => new VideoFormat(1152, 1536),
            RealtimeCameraPreset.Resolution1536x1152 => new VideoFormat(1536, 1152),
            _ => throw new ArgumentOutOfRangeException(nameof(preset))
        };

    internal Task CompleteLocalCameraPreviewAsync(ulong revision) =>
        Run(value => NativeError.Check(NativeMethods.xmax_realtime_complete_camera_preview(value, revision)));

    /// <summary>Lists the available camera devices without starting capture.</summary>
    public Task<IReadOnlyList<CameraDevice>> ListCameraDevicesAsync(
        CancellationToken cancellationToken = default
    ) => operations.Run(EnumerateCameraDevices, cancellationToken);

    private static unsafe IReadOnlyList<CameraDevice> EnumerateCameraDevices(nint realtime)
    {
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_realtime_list_cameras(realtime, &value));
        return XmaxClient.ReadCameraList(value);
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
