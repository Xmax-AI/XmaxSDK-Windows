//
//  MediaTime.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// A rational timestamp or duration on a media timeline.
/// </summary>
/// <param name="Value">The signed number of time units.</param>
/// <param name="Timescale">The number of units per second; zero indicates unavailable time.</param>
public readonly record struct MediaTime(
    long Value,
    int Timescale
)
{

    /// <summary>
    /// Gets this media time in seconds, or null when Timescale is not positive.
    /// </summary>
    public double? Seconds => Timescale > 0 ? (double)Value / Timescale : null;
}
