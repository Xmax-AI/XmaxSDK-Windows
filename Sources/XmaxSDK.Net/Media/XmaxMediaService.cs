//
//  XmaxMediaService.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>Calculates media and generation formats using the realtime model's rules.</summary>
public sealed class XmaxMediaService
{
    /// <summary>Gets the realtime model behind this service.</summary>
    public ModelInfo Model { get; }

    internal XmaxMediaService(ModelInfo model) => Model = model;

    /// <summary>Validates a camera preset or normalizes external media to the model area and alignment.</summary>
    public unsafe VideoFormat ResolveMediaSize(
        VideoFormat source,
        RealtimeMediaSource mediaSource = RealtimeMediaSource.External
    )
    {
        int camera = mediaSource switch
        {
            RealtimeMediaSource.Camera => 1,
            RealtimeMediaSource.External or RealtimeMediaSource.Video or RealtimeMediaSource.Image => 0,
            _ => throw new ArgumentOutOfRangeException(nameof(mediaSource))
        };
        using var model = new Utf8(Model.Identifier);
        var input = source.Native;
        xmax_video_format result;
        NativeError.Check(NativeMethods.xmax_resolve_media_format(model.Pointer, &input, camera, &result));
        return VideoFormat.From(result);
    }

    /// <summary>Selects the model generation canvas closest to the media aspect ratio.</summary>
    public unsafe VideoFormat ResolveGenerationSize(VideoFormat source)
    {
        using var model = new Utf8(Model.Identifier);
        var input = source.Native;
        xmax_video_format result;
        NativeError.Check(NativeMethods.xmax_resolve_generation_format(model.Pointer, &input, &result));
        return VideoFormat.From(result);
    }

    /// <summary>Resolves return dimensions within the tier limits.</summary>
    public unsafe VideoFormat ResolveTargetSize(
        VideoFormat source,
        double maximumWidth,
        double maximumHeight
    )
    {
        var input = source.Native;
        xmax_video_format result;
        NativeError.Check(NativeMethods.xmax_resolve_downlink_format(
            &input,
            maximumWidth,
            maximumHeight,
            &result
        ));
        return VideoFormat.From(result);
    }
}
