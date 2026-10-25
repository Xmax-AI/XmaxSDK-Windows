//
//  XmaxCapabilities.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// Features implemented by the loaded native SDK build.
/// </summary>
[Flags]
public enum XmaxCapabilities : ulong
{

    /// <summary>
    /// No optional features are reported.
    /// </summary>
    None = 0,

    /// <summary>
    /// Native RTC connection and video transport are available.
    /// </summary>
    Rtc = 1,

    /// <summary>
    /// Native camera capture is implemented; device availability and permission are checked separately.
    /// </summary>
    Camera = 2,

    /// <summary>
    /// CPU/GDI video presentation to a Windows HWND is implemented.
    /// </summary>
    NativeWindow = 4,

    /// <summary>
    /// SDK-managed uploads and downloads are available.
    /// </summary>
    Storage = 8,

    /// <summary>
    /// Trajectory mapping, signaling, and effect-data queries are available.
    /// </summary>
    Trajectory = 32,

    /// <summary>
    /// Serialized remote audio/video callbacks are available.
    /// </summary>
    RemoteMediaOutput = 64,

    /// <summary>
    /// Source-bound external PCM upload is available.
    /// </summary>
    ExternalAudioInput = 128
}
