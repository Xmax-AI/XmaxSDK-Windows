//
//  RealtimeEventType.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The category determining how to interpret a realtime event.
/// </summary>
public enum RealtimeEventType
{

    /// <summary>
    /// A lifecycle state change.
    /// </summary>
    State = 1,

    /// <summary>
    /// A reported realtime failure.
    /// </summary>
    Error = 2,

    /// <summary>
    /// Directional video and network statistics.
    /// </summary>
    Statistics = 3,

    /// <summary>
    /// Startup-stage durations or failed-startup timing.
    /// </summary>
    Timing = 4,

    /// <summary>
    /// An automatic or requested quality-level change.
    /// </summary>
    Quality = 5,

    /// <summary>
    /// A device-performance limitation or recovery.
    /// </summary>
    Performance = 6
}
