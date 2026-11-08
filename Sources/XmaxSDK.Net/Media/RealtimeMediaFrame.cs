//
//  RealtimeMediaFrame.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>An owned remote packet on the shared receiver rendering timeline.</summary>
/// <remarks>Dispose the packet after use or transfer ownership. Delivery is serialized.
/// Schedule both tracks by presentation time and check for discontinuities.</remarks>
public abstract class RealtimeMediaFrame : IDisposable
{
    private protected NativeHandle Handle { get; }

    /// <summary>Gets the opaque identity shared by both tracks of the result binding.</summary>
    public ulong StreamId { get; }

    /// <summary>Gets whether the track begins a run, changes clock or format, or follows discarded output.</summary>
    public bool IsDiscontinuous { get; }

    /// <summary>Gets the presentation time on the common receiver timeline.</summary>
    public MediaTime PresentationTime { get; }

    /// <summary>Gets the exact audio duration or a suggested video duration, which may be unavailable.</summary>
    public MediaTime Duration { get; }

    private protected RealtimeMediaFrame(NativeHandle handle, xmax_media_frame_view view)
    {
        Handle = handle;
        StreamId = view.stream_id;
        IsDiscontinuous = view.is_discontinuous != 0;
        PresentationTime = new(view.presentation_time.value, view.presentation_time.timescale);
        Duration = new(view.duration.value, view.duration.timescale);
    }

    internal static RealtimeMediaFrame TakeOwnership(nint value)
    {
        var handle = new NativeHandle(value, HandleKind.MediaFrame);
        try
        {
            var view = ReadView(value);
            return (MediaType)view.type switch
            {
                MediaType.Audio => new RealtimeAudioFrame(handle, view),
                MediaType.Video => new RealtimeVideoFrame(handle, view),
                _ => throw new NotSupportedException("The SDK returned an unknown remote media frame type.")
            };
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }

    private protected static unsafe xmax_media_frame_view ReadView(nint value)
    {
        var view = new xmax_media_frame_view { struct_size = (uint)sizeof(xmax_media_frame_view) };
        NativeError.Check(NativeMethods.xmax_media_frame_get_view(value, &view));
        return view;
    }

    /// <summary>Releases this packet and its retained native storage.</summary>
    public void Dispose() => Handle.Dispose();
}
