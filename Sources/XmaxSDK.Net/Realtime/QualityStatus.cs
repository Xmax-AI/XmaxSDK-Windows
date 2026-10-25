//
//  QualityStatus.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// A snapshot of independent uplink and downlink quality settings.
/// </summary>
/// <param name="AdaptiveEnabled">Whether automatic network-based adaptation is enabled.</param>
/// <param name="UplinkLevel">The active uplink level, from 1 for highest quality through 5; 0 means inactive.</param>
/// <param name="DownlinkLevel">The active downlink level, from 1 for highest quality through 5; 0 means inactive.</param>
/// <param name="Uplink">The effective RTC encoding format.</param>
/// <param name="Downlink">The requested result-stream format.</param>
public sealed record QualityStatus(
    bool AdaptiveEnabled,
    int UplinkLevel,
    int DownlinkLevel,
    VideoFormat Uplink,
    VideoFormat Downlink
);
