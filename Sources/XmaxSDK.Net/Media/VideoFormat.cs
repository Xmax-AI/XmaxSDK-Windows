//
//  VideoFormat.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>
/// The video dimensions and nominal frame rate for media input or encoding.
/// </summary>
/// <param name="Width">The width, in pixels.</param>
/// <param name="Height">The height, in pixels.</param>
/// <param name="FramesPerSecond">The nominal frame rate, in frames per second.</param>
/// <remarks>
/// Camera formats describe encoding targets; capture may use different device-supported dimensions.
/// External input frames must match their bound format. Query model capabilities before selecting a camera target.
/// </remarks>
public readonly record struct VideoFormat(
    uint Width,
    uint Height,
    uint FramesPerSecond = 30
)
{
    internal xmax_video_format Native => new() { width = Width, height = Height, fps = FramesPerSecond };

    internal static VideoFormat From(xmax_video_format value) => new(value.width, value.height, value.fps);

    /// <summary>
    /// Formats dimensions and nominal frame rate for display.
    /// </summary>
    /// <returns>A representation in pixels and frames per second.</returns>
    public override string ToString() => $"{Width} × {Height} @ {FramesPerSecond} fps";
}
