//
//  DownlinkVideoStatistics.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>RTC downlink video measurements. Unavailable numeric measurements are null.</summary>
/// <param name="UserId">The remote result user identifier.</param>
/// <param name="Width">The decoded video width, in pixels.</param>
/// <param name="Height">The decoded video height, in pixels.</param>
/// <param name="DecodeFrameRate">The decoder output frame rate.</param>
/// <param name="RenderFrameRate">The RTC render frame rate.</param>
/// <param name="BitrateKbps">The incoming bitrate in kbps.</param>
/// <param name="PacketLossPercent">The incoming packet-loss percentage.</param>
/// <param name="EndToEndDelayMs">The RTC end-to-end delay in milliseconds.</param>
public sealed record DownlinkVideoStatistics(
    string UserId,
    int? Width,
    int? Height,
    int? DecodeFrameRate,
    int? RenderFrameRate,
    int? BitrateKbps,
    int? PacketLossPercent,
    int? EndToEndDelayMs
);
