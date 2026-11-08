//
//  RealtimeCamera.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Selects the input for a local camera stream.</summary>
public sealed record RealtimeCamera
{
    /// <summary>Uses the system default camera when the stream is created.</summary>
    public static RealtimeCamera SystemDefault { get; } = new(deviceId: null);

    internal string? DeviceId { get; }

    private RealtimeCamera(string? deviceId) => DeviceId = deviceId;

    /// <summary>Selects a camera by the identifier returned by device enumeration.</summary>
    public static RealtimeCamera Device(string deviceId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(deviceId);
        return new(deviceId);
    }
}
