//
//  RealtimeMediaSource.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The source type used to resolve a model's media dimensions.</summary>
public enum RealtimeMediaSource
{
    /// <summary>A local camera with a model-supported resolution preset.</summary>
    Camera,

    /// <summary>Frames supplied by an application.</summary>
    External,

    /// <summary>Media decoded from a video file.</summary>
    Video,

    /// <summary>Media decoded from an image file.</summary>
    Image
}
