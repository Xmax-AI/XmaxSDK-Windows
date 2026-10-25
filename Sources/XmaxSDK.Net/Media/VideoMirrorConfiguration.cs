//
//  VideoMirrorConfiguration.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// Independent mirror settings for preview, result display, and published video.
/// </summary>
/// <param name="LocalPreview">Whether local frame metadata requests a horizontal display flip.</param>
/// <param name="RemoteView">Whether result frame metadata requests a horizontal display flip.</param>
/// <param name="PublishedVideo">Whether the RTC encoder mirrors published video sent to the service.</param>
/// <remarks>
/// Display mirroring leaves stored pixels unchanged. Apply the frame mirror flag once in custom renderers.
/// </remarks>
public readonly record struct VideoMirrorConfiguration(
    bool LocalPreview,
    bool RemoteView,
    bool PublishedVideo
);
