//
//  RealtimeAudioFrame.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>An owned interleaved PCM16 packet from the result stream.</summary>
public sealed class RealtimeAudioFrame : RealtimeMediaFrame
{
    /// <summary>Gets the number of samples per second in each channel.</summary>
    public uint SampleRate { get; }

    /// <summary>Gets the interleaved channel count.</summary>
    public uint Channels { get; }

    /// <summary>Gets the number of samples per channel in this packet.</summary>
    public uint SamplesPerChannel { get; }

    internal RealtimeAudioFrame(NativeHandle handle, xmax_media_frame_view view) : base(handle, view)
    {
        SampleRate = view.audio.sample_rate;
        Channels = view.audio.channels;
        SamplesPerChannel = view.audio.samples_per_channel;
    }

    /// <summary>Reads the borrowed PCM bytes synchronously. The pointer is valid during the callback.</summary>
    /// <param name="reader">Receives the PCM pointer and its accessible byte count.</param>
    public unsafe void ReadPcm16(Action<nint, int> reader)
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = Handle.Borrow();
        var audio = ReadView(lease.Value).audio;
        reader((nint)audio.data, checked((int)audio.data_size));
    }
}
