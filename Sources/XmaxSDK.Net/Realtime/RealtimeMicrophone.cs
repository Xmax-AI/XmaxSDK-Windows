//
//  RealtimeMicrophone.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The initial microphone captured when a local camera stream connects.</summary>
public sealed record RealtimeMicrophone
{
    /// <summary>Disables microphone capture and publication.</summary>
    public static RealtimeMicrophone Disabled { get; } = new(null, false);

    /// <summary>Uses the system default recording input.</summary>
    public static RealtimeMicrophone SystemDefault { get; } = new(null, true);

    /// <summary>Gets the explicitly selected Windows recording endpoint ID.</summary>
    public string? DeviceId { get; }

    /// <summary>Gets whether camera connection should capture microphone audio.</summary>
    public bool IsEnabled { get; }

    private RealtimeMicrophone(string? deviceId, bool enabled)
    {
        DeviceId = deviceId;
        IsEnabled = enabled;
    }

    /// <summary>Selects a Windows recording endpoint by its stable device ID.</summary>
    /// <param name="deviceId">The nonempty Windows recording endpoint identifier.</param>
    /// <returns>An enabled explicit microphone selection.</returns>
    public static RealtimeMicrophone Device(string deviceId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(deviceId);
        return new(deviceId, true);
    }
}
