//
//  RealtimeQualityLevel.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The transport quality target, independent of observed network quality.
/// </summary>
public enum RealtimeQualityLevel
{
    /// <summary>
    /// Uses the baseline width and height.
    /// </summary>
    L1 = 1,

    /// <summary>
    /// Targets 90% of the baseline width and height.
    /// </summary>
    L2 = 2,

    /// <summary>
    /// Targets 75% of the baseline width and height.
    /// </summary>
    L3 = 3,

    /// <summary>
    /// Targets 60% of the baseline width and height.
    /// </summary>
    L4 = 4,

    /// <summary>
    /// Targets 50% of the baseline width and height.
    /// </summary>
    L5 = 5
}
