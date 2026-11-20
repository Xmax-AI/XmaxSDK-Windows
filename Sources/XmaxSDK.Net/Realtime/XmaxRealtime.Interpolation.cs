//
//  XmaxRealtime.Interpolation.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

internal sealed partial class XmaxRealtime
{
    /// <summary>Configures two-times interpolation for the result preview. Null disables it.</summary>
    public Task SetRemoteVideoInterpolationAsync(RealtimeVideoInterpolationOptions? options) =>
        Run(value => SetInterpolation(value, options));

    private static unsafe void SetInterpolation(nint realtime, RealtimeVideoInterpolationOptions? options)
    {
        using var directory = new Utf8(options?.ModelDirectory);
        var native = new xmax_video_interpolation_options
        {
            struct_size = (uint)sizeof(xmax_video_interpolation_options),
            model_directory = directory.Pointer,
            device_index = options?.DeviceIndex ?? 0,
            cpu_threads = options?.CpuThreads ?? 4,
            buffer_ms = options?.BufferMilliseconds ?? 125
        };
        NativeError.Check(NativeMethods.xmax_realtime_set_remote_video_interpolation(realtime, options is null ? null : &native));
    }

    /// <summary>Reads interpolation state and recoverable errors without waiting for inference.</summary>
    public Task<RealtimeVideoInterpolationStatus> GetRemoteVideoInterpolationStatusAsync() =>
        operations.Run(ReadInterpolationStatus);

    private static unsafe RealtimeVideoInterpolationStatus ReadInterpolationStatus(nint realtime)
    {
        var status = new xmax_video_interpolation_status { struct_size = (uint)sizeof(xmax_video_interpolation_status) };
        NativeError.Check(NativeMethods.xmax_realtime_get_remote_video_interpolation_status(realtime, &status));
        XmaxException? error = status.error_code < 0 ? new(status.error_code, NativeError.Text(status.error_message)) : null;
        return new(status.enabled != 0, status.input_frames, status.interpolated_frames, status.skipped_frames, error);
    }
}
