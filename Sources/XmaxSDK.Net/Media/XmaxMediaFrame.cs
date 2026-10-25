using Xmax.SDK.Interop;

namespace Xmax.SDK;


/// <summary>An owned remote packet. Dispose after reading or transferring ownership to a consumer.</summary>
public sealed class XmaxMediaFrame : IDisposable
{
    private readonly NativeHandle handle;
    public MediaType Type { get; }
    public ulong StreamId { get; }
    public bool IsDiscontinuous { get; }
    public MediaTime PresentationTime { get; }
    public MediaTime Duration { get; }
    public uint SampleRate { get; }
    public uint Channels { get; }
    public uint SamplesPerChannel { get; }
    public uint VideoWidth { get; }
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
    public unsafe void ReadPcm16(Action<nint, int> reader)
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = handle.Borrow();
        var view = View(lease.Value);
        if (view.type != 1) throw new InvalidOperationException("Packet does not contain PCM audio.");
        reader((nint)view.audio.data, checked((int)view.audio.data_size));
    }
    public unsafe void ReadBgra(Action<nint, int, int> reader)
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = handle.Borrow();
        var view = View(lease.Value);
        if (view.type != 2 || view.video.pixel_format != 1) throw new NotSupportedException("Packet does not contain BGRA video.");
        reader((nint)view.video.planes_0, checked((int)view.video.strides_0), checked((int)view.video.plane_sizes_0));
    }
    public void Dispose() => handle.Dispose();
}
