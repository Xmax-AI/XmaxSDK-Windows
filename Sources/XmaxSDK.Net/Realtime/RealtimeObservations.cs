//
//  RealtimeObservations.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The timing of realtime startup stages in milliseconds.</summary>
public sealed record RealtimeLaunchTiming(
    double? SessionMilliseconds,
    double? RtcJoinMilliseconds,
    double? PublishMilliseconds,
    double? FirstFrameMilliseconds,
    double? TotalMilliseconds
);

/// <summary>Observed quality of one realtime network direction.</summary>
public enum RealtimeNetworkQualityLevel
{
    /// <summary>No network sample is available.</summary>
    Unknown,
    /// <summary>The network is excellent.</summary>
    Excellent,
    /// <summary>The network is good.</summary>
    Good,
    /// <summary>The network may affect media delivery.</summary>
    Poor,
    /// <summary>The network is noticeably degraded.</summary>
    Bad,
    /// <summary>The network may interrupt media delivery.</summary>
    VeryBad,
    /// <summary>The connection is down.</summary>
    Down
}

/// <summary>Observed upstream and downstream network quality.</summary>
public sealed record RealtimeNetworkQuality(RealtimeNetworkQualityLevel Uplink, RealtimeNetworkQualityLevel Downlink);

/// <summary>A device performance limit or recovery and its optional suggested format.</summary>
public sealed record RealtimePerformanceAlarm(RealtimePerformanceStatus Status, VideoFormat? SuggestedVideoFormat);

/// <summary>The current uplink encoding performance state.</summary>
public enum RealtimePerformanceStatus
{
    /// <summary>The encoder is constrained by available processing capacity.</summary>
    Limited,
    /// <summary>The encoder has recovered from a processing constraint.</summary>
    Recovered
}
