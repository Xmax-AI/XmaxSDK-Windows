using System.Runtime.InteropServices;
using Xmax.SDK.Interop;

namespace Xmax.SDK;


public sealed class XmaxVideoFrame : IDisposable
{
    private readonly NativeHandle handle;
    private readonly uint width, height;
    public uint Width => width;
    public uint Height => height;
    public long TimestampMicroseconds { get; }
    public VideoPixelFormat PixelFormat { get; }
    public bool Mirrored { get; }
    internal unsafe XmaxVideoFrame(NativeHandle handle)
    {
        this.handle = handle;
        try
        {
            using var lease = handle.Borrow();
            var view = new xmax_video_frame { struct_size = (uint)sizeof(xmax_video_frame) };
            NativeError.Check(NativeMethods.xmax_frame_get_view(lease.Value, &view));
            int mirrored = 0;
            NativeError.Check(NativeMethods.xmax_frame_get_mirrored(lease.Value, &mirrored));
            width = view.width; height = view.height; TimestampMicroseconds = view.timestamp_us;
            PixelFormat = (VideoPixelFormat)view.pixel_format; Mirrored = mirrored != 0;
        }
        catch { handle.Dispose(); throw; }
    }
    /// <summary>Invokes a synchronous reader while retaining pixels. Never retain the pointer after return.</summary>
    public unsafe void ReadBgra(Action<nint, int, int> reader)
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = handle.Borrow();
        var view = new xmax_video_frame { struct_size = (uint)sizeof(xmax_video_frame) };
        NativeError.Check(NativeMethods.xmax_frame_get_view(lease.Value, &view));
        if (view.pixel_format != 1) throw new NotSupportedException("Frame is not BGRA; use ReadPlane for planar pixels.");
        reader((nint)view.planes_0, checked((int)view.strides_0), checked((int)view.plane_sizes_0));
    }
    public byte[] CopyBgra()
    {
        byte[]? result = null;
        ReadBgra((pointer, stride, size) => { result = new byte[size]; Marshal.Copy(pointer, result, 0, size); });
        return result!;
    }
    /// <summary>Reads a plane synchronously. NV12 has two planes, I420 three; BGRA has one.</summary>
    public unsafe void ReadPlane(int index, Action<nint, int, int> reader)
    {
        ArgumentNullException.ThrowIfNull(reader);
        int planes = PixelFormat switch { VideoPixelFormat.Bgra => 1, VideoPixelFormat.Nv12 => 2, _ => 3 };
        if (index < 0 || index >= planes) throw new ArgumentOutOfRangeException(nameof(index));
        using var lease = handle.Borrow();
        var view = new xmax_video_frame { struct_size = (uint)sizeof(xmax_video_frame) };
        NativeError.Check(NativeMethods.xmax_frame_get_view(lease.Value, &view));
        byte** pointers = &view.planes_0;
        nuint* strides = &view.strides_0;
        nuint* sizes = &view.plane_sizes_0;
        reader((nint)pointers[index], checked((int)strides[index]), checked((int)sizes[index]));
    }
    public void Dispose() => handle.Dispose();
}
