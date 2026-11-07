//
//  MicrophoneDevice.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>A physical or virtual Windows recording endpoint.</summary>
/// <param name="Id">The endpoint identifier used to select this microphone.</param>
/// <param name="Name">The device name displayed by Windows.</param>
/// <param name="IsDefault">Whether Windows selected this endpoint as the default input.</param>
public sealed record MicrophoneDevice(string Id, string Name, bool IsDefault);
