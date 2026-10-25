//
//  VideoPixelFormat.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The CPU pixel layout of a video frame.
/// </summary>
public enum VideoPixelFormat
{

    /// <summary>
    /// Packed blue, green, red, and alpha bytes in one plane.
    /// </summary>
    Bgra = 1,

    /// <summary>
    /// A full-size luma plane followed by an interleaved UV chroma plane.
    /// </summary>
    Nv12,

    /// <summary>
    /// Separate luma, U chroma, and V chroma planes.
    /// </summary>
    I420
}
