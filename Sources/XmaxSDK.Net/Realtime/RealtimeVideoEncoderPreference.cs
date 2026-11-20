//
//  RealtimeVideoEncoderPreference.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The encoder tradeoff when network or device conditions are constrained.
/// </summary>
public enum RealtimeVideoEncoderPreference
{

    /// <summary>
    /// Prefers maintaining frame rate.
    /// </summary>
    MaintainFramerate = 1,

    /// <summary>
    /// Prefers maintaining image quality.
    /// </summary>
    MaintainQuality = 2
}
