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

internal sealed class XmaxMediaService(RealtimeModel model) : IMediaServicing
{
    public RealtimeModel Model { get; } = model;

    public unsafe MediaSize ResolveMediaSize(MediaSize size, RealtimeMediaSource source)
    {
        int camera = source switch
        {
            RealtimeMediaSource.Camera => 1,
            RealtimeMediaSource.External or RealtimeMediaSource.Video or RealtimeMediaSource.Image => 0,
            _ => throw new ArgumentOutOfRangeException(nameof(source))
        };
        using var modelName = new Utf8(XmaxClient.GetModelIdentifier(Model));
        var input = size.Native;
        NativeError.Check(NativeMethods.xmax_resolve_media_size(modelName.Pointer, &input, camera, &input));
        return MediaSize.From(input);
    }

    public unsafe MediaSize ResolveGenerationSize(MediaSize size)
    {
        using var modelName = new Utf8(XmaxClient.GetModelIdentifier(Model));
        var input = size.Native;
        NativeError.Check(NativeMethods.xmax_resolve_generation_size(modelName.Pointer, &input, &input));
        return MediaSize.From(input);
    }

    public unsafe MediaSize ResolveTargetSize(MediaSize source, MediaSize maximumSize)
    {
        var input = source.Native;
        var maximum = maximumSize.Native;
        NativeError.Check(NativeMethods.xmax_resolve_target_size(&input, &maximum, &input));
        return MediaSize.From(input);
    }
}
