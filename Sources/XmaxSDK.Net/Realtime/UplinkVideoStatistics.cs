//
//  UplinkVideoStatistics.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>RTC uplink video measurements. Unavailable numeric measurements are null.</summary>
/// <param name="Width">The encoded video width, in pixels.</param>
/// <param name="Height">The encoded video height, in pixels.</param>
/// <param name="CaptureFrameRate">The source capture frame rate.</param>
/// <param name="EncodeFrameRate">The encoder output frame rate.</param>
/// <param name="SendFrameRate">The transmitted video frame rate.</param>
/// <param name="BitrateKbps">The outgoing bitrate in kbps.</param>
/// <param name="PacketLossPercent">The outgoing packet-loss percentage.</param>
public sealed record UplinkVideoStatistics(
    int? Width,
    int? Height,
    int? CaptureFrameRate,
    int? EncodeFrameRate,
    int? SendFrameRate,
    int? BitrateKbps,
    int? PacketLossPercent
);
