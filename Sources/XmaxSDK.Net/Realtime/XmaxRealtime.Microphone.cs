//
//  XmaxRealtime.Microphone.cs
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
    /// <summary>Lists recording endpoints without starting capture or requesting permission.</summary>
    public Task<IReadOnlyList<MicrophoneDevice>> ListMicrophoneDevicesAsync(
        CancellationToken cancellationToken = default
    ) => operations.Run(_ => MediaDevices.ReadMicrophoneSnapshot(), cancellationToken);

    /// <summary>Changes the selected camera microphone without restarting video generation.</summary>
    /// <param name="deviceId">The Windows recording endpoint ID, or null to follow the default.</param>
    public Task SwitchMicrophoneAsync(string? deviceId) => Run(value => SwitchMicrophone(value, deviceId));

    /// <summary>Changes camera microphone capture and publication without restarting video generation.</summary>
    public Task SetMicrophoneEnabledAsync(bool enabled) => Run(
        value => NativeError.Check(NativeMethods.xmax_realtime_set_microphone_enabled(value, enabled ? 1 : 0))
    );

    /// <summary>Reads whether the camera stream has enabled microphone capture.</summary>
    public Task<bool> GetMicrophoneEnabledAsync() => operations.Run(ReadMicrophoneEnabled);

    private static unsafe void SwitchMicrophone(nint value, string? deviceId)
    {
        using var device = new Utf8(deviceId);
        NativeError.Check(NativeMethods.xmax_realtime_switch_microphone(value, device.Pointer));
    }

    private static unsafe bool ReadMicrophoneEnabled(nint value)
    {
        int enabled;
        NativeError.Check(NativeMethods.xmax_realtime_get_microphone_enabled(value, &enabled));
        return enabled != 0;
    }
}
