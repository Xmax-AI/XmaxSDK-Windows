//
//  LogOptions.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// Process-wide diagnostic categories; logging is disabled by default.
/// </summary>
[Flags]
public enum LogOptions : uint
{

    /// <summary>
    /// Disables logging and clears queued records when configured.
    /// </summary>
    None = 0,

    /// <summary>
    /// Enables session, media, API, and storage diagnostics.
    /// </summary>
    Business = 1,

    /// <summary>
    /// Enables RTC statistics, network-quality, and device-performance diagnostics.
    /// </summary>
    Performance = 2
}
