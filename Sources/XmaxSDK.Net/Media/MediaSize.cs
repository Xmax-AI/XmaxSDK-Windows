//
//  MediaSize.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>Media dimensions in pixels, independent of frame rate and encoding.</summary>
public readonly record struct MediaSize(double Width, double Height)
{
    internal xmax_media_size Native => new() { width = Width, height = Height };
    internal static MediaSize From(xmax_media_size value) => new(value.width, value.height);
}
