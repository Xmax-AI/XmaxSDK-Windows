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
    internal string? DeviceId { get; }
    internal CameraPosition? PreferredPosition { get; }

    private RealtimeCamera(string? deviceId, CameraPosition? position = null) { DeviceId = deviceId; PreferredPosition = position; }

    /// <summary>Prefers a front or rear camera, falling back to the system default if unavailable.</summary>
    /// <param name="position">Front or Back.</param>
    /// <returns>A selection by facing direction.</returns>
    public static RealtimeCamera Position(CameraPosition position)
    {
        if (position is not (CameraPosition.Front or CameraPosition.Back)) throw new ArgumentOutOfRangeException(nameof(position));
        return new(null, position);
    }

    /// <summary>Selects a camera by the identifier returned by device enumeration.</summary>
    public static RealtimeCamera Device(string deviceId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(deviceId);
        return new(deviceId);
    }
}
