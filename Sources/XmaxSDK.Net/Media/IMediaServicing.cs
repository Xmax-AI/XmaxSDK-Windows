//
//  IMediaServicing.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Resolves media and generation dimensions for a realtime model.</summary>
public interface IMediaServicing
{
    /// <summary>The model used for the size rules.</summary>
    RealtimeModel Model { get; }

    /// <summary>Validates camera dimensions or aligns input media within the model's pixel limits.</summary>
    MediaSize ResolveMediaSize(MediaSize size, RealtimeMediaSource source);

    /// <summary>Selects the model canvas closest to the input aspect ratio.</summary>
    MediaSize ResolveGenerationSize(MediaSize size);

    /// <summary>Fits the source within the supplied bounds without upscaling.</summary>
    MediaSize ResolveTargetSize(MediaSize source, MediaSize maximumSize);
}
