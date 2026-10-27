//
//  RealtimeConfiguration.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The model and initial automatic-quality policy for one realtime controller.
/// </summary>
public sealed record RealtimeConfiguration
{

    /// <summary>
    /// Gets the service model identifier: x2.1 (default).
    /// </summary>
    public string Model { get; init; } = "x2.1";

    /// <summary>
    /// Gets whether independent uplink and downlink adaptation is enabled; the default is true.
    /// </summary>
    /// <remarks>
    /// Disabled connections start at the highest quality level and ignore persisted quality history; manual level changes remain available.
    /// </remarks>
    public bool AdaptiveQualityEnabled { get; init; } = true;
}
