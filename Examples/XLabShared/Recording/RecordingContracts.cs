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
    private readonly XmaxMediaFrame frame;
    public MediaType Type => frame.Type;
    public ulong StreamId => frame.StreamId;
    public MediaTime Time => frame.PresentationTime;
    public MediaTime Duration => frame.Duration;
    public int Width => checked((int)frame.VideoWidth);
    public int Height => checked((int)frame.VideoHeight);
    public int SampleRate => checked((int)frame.SampleRate);
    public int Channels => checked((int)frame.Channels);
    public int Samples => checked((int)frame.SamplesPerChannel);
    public int ByteCount { get; }
    internal RemoteRecordingPacket(XmaxMediaFrame frame)
    {
        this.frame = frame;
        int count = 0;
        if (Type == MediaType.Video) frame.ReadBgra((_, _, size) => count = size);
        else if (Type == MediaType.Audio) frame.ReadPcm16((_, size) => count = size);
        ByteCount = count;
    }
    public void ReadVideo(Action<nint, int, int> read) => frame.ReadBgra(read);
    public void ReadAudio(Action<nint, int> read) => frame.ReadPcm16(read);
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
