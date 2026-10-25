//
//  XmaxMediaFrame.cs
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
/// An owned remote PCM or video packet on the shared receiver rendering timeline.
/// </summary>
/// <remarks>
/// Dispose after use or transfer ownership to your consumer. Only metadata for Type is populated; fields of the other variant are zero.
/// Serialized callback order is not cross-track presentation order. Schedule audio and video by PresentationTime and inspect IsDiscontinuous for timeline changes.
/// </remarks>
public sealed class XmaxMediaFrame : IDisposable
{
    private readonly NativeHandle handle;

    /// <summary>
    /// Gets the audio or video variant carried by this packet.
    /// </summary>
    public MediaType Type { get; }

    /// <summary>
    /// Gets the identity shared by tracks belonging to this output stream.
    /// </summary>
    public ulong StreamId { get; }

    /// <summary>
    /// Gets whether this track starts, changes format or clock, or follows discarded output.
    /// </summary>
    public bool IsDiscontinuous { get; }

    /// <summary>
    /// Gets the first audio-sample or video presentation time on the common rendering timeline.
    /// </summary>
    public MediaTime PresentationTime { get; }

    /// <summary>
    /// Gets the exact audio duration; a video duration may be unavailable when its timescale is zero.
    /// </summary>
    public MediaTime Duration { get; }

    /// <summary>
    /// Gets the audio sampling rate, in samples per second per channel.
    /// </summary>
    public uint SampleRate { get; }

    /// <summary>
    /// Gets the interleaved audio channel count.
    /// </summary>
    public uint Channels { get; }

    /// <summary>
    /// Gets the number of samples per audio channel in this packet.
    /// </summary>
    public uint SamplesPerChannel { get; }

    /// <summary>
    /// Gets the decoded video width, in pixels.
    /// </summary>
    public uint VideoWidth { get; }

    /// <summary>
    /// Gets the decoded video height, in pixels.
    /// </summary>
    public uint VideoHeight { get; }

    internal unsafe XmaxMediaFrame(nint value)
    {
        handle = new(value, HandleKind.MediaFrame);
        try
        {
            var view = View(value);
            Type = (MediaType)view.type; StreamId = view.stream_id; IsDiscontinuous = view.is_discontinuous != 0;
            PresentationTime = new(view.presentation_time.value, view.presentation_time.timescale);
            Duration = new(view.duration.value, view.duration.timescale);
            SampleRate = view.audio.sample_rate; Channels = view.audio.channels; SamplesPerChannel = view.audio.samples_per_channel;
            VideoWidth = view.video.width; VideoHeight = view.video.height;
        }
        catch { handle.Dispose(); throw; }
    }

    private static unsafe xmax_media_frame_view View(nint value)
    {
        var view = new xmax_media_frame_view { struct_size = (uint)sizeof(xmax_media_frame_view) };
        NativeError.Check(NativeMethods.xmax_media_frame_get_view(value, &view));
        return view;
    }

    /// <summary>
    /// Borrows the packet's interleaved PCM16 bytes for a synchronous reader.
    /// </summary>
    /// <param name="reader">A synchronous callback receiving the borrowed PCM pointer and byte count; never retain the pointer after return.</param>
    /// <exception cref="InvalidOperationException">The packet does not contain audio.</exception>
    public unsafe void ReadPcm16(
        Action<nint, int> reader
    )
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = handle.Borrow();
        var view = View(lease.Value);
        if (view.type != 1) throw new InvalidOperationException("Packet does not contain PCM audio.");
        reader((nint)view.audio.data, checked((int)view.audio.data_size));
    }

    /// <summary>
    /// Borrows the packet's decoded BGRA plane for a synchronous reader.
    /// </summary>
    /// <param name="reader">A synchronous callback receiving the borrowed pixel pointer, row stride in bytes, and accessible byte count; never retain the pointer after return.</param>
    /// <exception cref="NotSupportedException">The packet does not contain BGRA video.</exception>
    public unsafe void ReadBgra(
        Action<nint, int, int> reader
    )
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = handle.Borrow();
        var view = View(lease.Value);
        if (view.type != 2 || view.video.pixel_format != 1) throw new NotSupportedException("Packet does not contain BGRA video.");
        reader((nint)view.video.planes_0, checked((int)view.video.strides_0), checked((int)view.video.plane_sizes_0));
    }

    /// <summary>
    /// Releases this packet owner and its media when no owners remain.
    /// </summary>
    public void Dispose() => handle.Dispose();
}
