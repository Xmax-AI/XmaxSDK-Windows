//
//  XmaxVideoFrame.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Runtime.InteropServices;
using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>
/// An owned video frame whose pixels remain retained independently of its realtime controller.
/// </summary>
/// <remarks>
/// Dispose after use. Synchronous read callbacks borrow pixel pointers only for the duration of the callback.
/// </remarks>
public sealed class XmaxVideoFrame : IDisposable
{
    private readonly NativeHandle handle;

    private readonly uint width, height;

    /// <summary>
    /// Gets the actual frame width, in pixels.
    /// </summary>
    public uint Width => width;

    /// <summary>
    /// Gets the actual frame height, in pixels.
    /// </summary>
    public uint Height => height;

    /// <summary>
    /// Gets the frame timestamp, in microseconds on its source timeline.
    /// </summary>
    public long TimestampMicroseconds { get; }

    /// <summary>
    /// Gets the stored CPU pixel layout.
    /// </summary>
    public VideoPixelFormat PixelFormat { get; }

    /// <summary>
    /// Gets whether this frame requests one horizontal display flip; the stored pixels remain unchanged.
    /// </summary>
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

    /// <summary>
    /// Borrows a BGRA plane while retaining the native frame for a synchronous reader.
    /// </summary>
    /// <param name="reader">A synchronous callback receiving the borrowed pixel pointer, row stride in bytes, and accessible byte count; never retain the pointer after return.</param>
    /// <exception cref="NotSupportedException">The frame is not BGRA; use ReadPlane for planar formats.</exception>
    public unsafe void ReadBgra(
        Action<nint, int, int> reader
    )
    {
        ArgumentNullException.ThrowIfNull(reader);
        using var lease = handle.Borrow();
        var view = new xmax_video_frame { struct_size = (uint)sizeof(xmax_video_frame) };
        NativeError.Check(NativeMethods.xmax_frame_get_view(lease.Value, &view));
        if (view.pixel_format != 1) throw new NotSupportedException("Frame is not BGRA; use ReadPlane for planar pixels.");
        reader((nint)view.planes_0, checked((int)view.strides_0), checked((int)view.plane_sizes_0));
    }

    /// <summary>
    /// Copies the BGRA plane into caller-owned managed memory.
    /// </summary>
    /// <returns>A new byte array containing the plane bytes, including any row padding.</returns>
    /// <exception cref="NotSupportedException">The frame is not BGRA.</exception>
    public byte[] CopyBgra()
    {
        byte[]? result = null;
        ReadBgra((pointer, stride, size) => { result = new byte[size]; Marshal.Copy(pointer, result, 0, size); });
        return result!;
    }

    /// <summary>
    /// Borrows one stored video plane for a synchronous reader.
    /// </summary>
    /// <param name="index">The zero-based plane index: BGRA has one plane, NV12 has two, and I420 has three.</param>
    /// <param name="reader">A synchronous callback receiving the borrowed pixel pointer, row stride in bytes, and accessible byte count; never retain the pointer after return.</param>
    /// <exception cref="ArgumentOutOfRangeException">The plane index is outside this format's plane range.</exception>
    public unsafe void ReadPlane(
        int index,
        Action<nint, int, int> reader
    )
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

    /// <summary>
    /// Releases this frame owner and its retained pixels when no owners remain.
    /// </summary>
    public void Dispose() => handle.Dispose();
}
