//
//  RealtimeVideoFrame.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>An owned result video frame after SDK video processing.</summary>
public sealed class RealtimeVideoFrame : RealtimeMediaFrame
{
    /// <summary>Gets the decoded width in pixels.</summary>
    public uint Width { get; }

    /// <summary>Gets the decoded height in pixels.</summary>
    public uint Height { get; }

    internal RealtimeVideoFrame(NativeHandle handle, xmax_media_frame_view view) : base(handle, view)
    {
        Width = view.video.width;
        Height = view.video.height;
    }

    /// <summary>Reads the borrowed BGRA plane synchronously. The pointer is valid during the callback.</summary>
    /// <param name="reader">Receives the pixel pointer, row stride, and accessible byte count.</param>
    public unsafe void ReadBgra(Action<nint, int, int> reader)
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = Handle.Borrow();
        var video = ReadView(lease.Value).video;
        if (video.pixel_format != 1)
            throw new NotSupportedException("Packet does not contain BGRA video.");
        reader((nint)video.planes_0, checked((int)video.strides_0), checked((int)video.plane_sizes_0));
    }
}
