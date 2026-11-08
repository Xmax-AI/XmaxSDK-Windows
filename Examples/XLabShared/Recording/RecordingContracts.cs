//
//  RecordingContracts.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK;

namespace XLab.Shared.Recording;

/// <summary>Owned input retained until the recording worker finishes reading it.</summary>
internal interface IRecordingPacket : IDisposable
{
    MediaType Type { get; }
    ulong StreamId { get; }
    MediaTime Time { get; }
    MediaTime Duration { get; }
    int Width { get; }
    int Height { get; }
    int SampleRate { get; }
    int Channels { get; }
    int Samples { get; }
    int ByteCount { get; }
    void ReadVideo(Action<nint, int, int> read);
    void ReadAudio(Action<nint, int> read);
}

/// <summary>Adopts the SDK callback's retained packet without copying pixels on its delivery thread.</summary>
internal sealed class RemoteRecordingPacket : IRecordingPacket
{
    private readonly RealtimeMediaFrame frame;
    public MediaType Type => frame is RealtimeVideoFrame ? MediaType.Video : MediaType.Audio;
    public ulong StreamId => frame.StreamId;
    public MediaTime Time => frame.PresentationTime;
    public MediaTime Duration => frame.Duration;
    public int Width => frame is RealtimeVideoFrame video ? checked((int)video.Width) : 0;
    public int Height => frame is RealtimeVideoFrame video ? checked((int)video.Height) : 0;
    public int SampleRate => frame is RealtimeAudioFrame audio ? checked((int)audio.SampleRate) : 0;
    public int Channels => frame is RealtimeAudioFrame audio ? checked((int)audio.Channels) : 0;
    public int Samples => frame is RealtimeAudioFrame audio ? checked((int)audio.SamplesPerChannel) : 0;
    public int ByteCount { get; }
    internal RemoteRecordingPacket(RealtimeMediaFrame frame)
    {
        this.frame = frame;
        int count = 0;
        if (frame is RealtimeVideoFrame video) video.ReadBgra((_, _, size) => count = size);
        else if (frame is RealtimeAudioFrame audio) audio.ReadPcm16((_, size) => count = size);
        ByteCount = count;
    }
    public void ReadVideo(Action<nint, int, int> read) => ((RealtimeVideoFrame)frame).ReadBgra(read);
    public void ReadAudio(Action<nint, int> read) => ((RealtimeAudioFrame)frame).ReadPcm16(read);
    public void Dispose() => frame.Dispose();
}

/// <summary>Receives normalized sample times on one recording thread. Pixels are borrowed for the call.</summary>
internal interface IRecordingWriter : IDisposable
{
    void Open(string path, int width, int height);
    void Video(nint pixels, int stride, int size, int width, int height, long time, long duration);
    void Audio(ReadOnlySpan<byte> pcm, long time);
    void Finish();
}

internal sealed record RecordingResult(string? Path, string? Error);
