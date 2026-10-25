//
//  VideoContentMode.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// How video fits within a display viewport while retaining its aspect ratio.
/// </summary>
public enum VideoContentMode
{

    /// <summary>
    /// Displays the complete image, leaving margins when the aspect ratios differ.
    /// </summary>
    Fit,

    /// <summary>
    /// Fills the viewport, cropping image edges when the aspect ratios differ.
    /// </summary>
    Fill
}
