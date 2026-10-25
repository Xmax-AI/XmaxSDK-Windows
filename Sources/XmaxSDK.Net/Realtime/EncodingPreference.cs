//
//  EncodingPreference.cs
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
public enum EncodingPreference
{

    /// <summary>
    /// Lets the RTC encoder select its adaptation strategy.
    /// </summary>
    Automatic,

    /// <summary>
    /// Prefers maintaining frame rate.
    /// </summary>
    MaintainFramerate,

    /// <summary>
    /// Prefers maintaining image quality.
    /// </summary>
    MaintainQuality
}
