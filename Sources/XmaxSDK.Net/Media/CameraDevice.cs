//
//  CameraDevice.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// A camera enumeration snapshot used to select a capture device.
/// </summary>
/// <param name="Id">The device identifier to pass to camera preparation or switching.</param>
/// <param name="Name">The device display name; names need not be unique.</param>
/// <param name="IsDefault">Whether the operating system reports this device as the default.</param>
public sealed record CameraDevice(
    string Id,
    string Name,
    bool IsDefault
);
