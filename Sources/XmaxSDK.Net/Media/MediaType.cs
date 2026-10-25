//
//  MediaType.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// The content variant of a remote media packet.
/// </summary>
public enum MediaType
{

    /// <summary>
    /// Interleaved little-endian PCM16 audio.
    /// </summary>
    Audio = 1,

    /// <summary>
    /// Decoded video in its original pixel orientation.
    /// </summary>
    Video
}
