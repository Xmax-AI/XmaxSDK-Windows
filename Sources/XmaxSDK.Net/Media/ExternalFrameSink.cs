using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>Source-bound receiver. Push copies input before returning; old bindings become inert.</summary>
public sealed class ExternalFrameSink : IDisposable
{
    private readonly NativeHandle handle;
    public VideoFormat Format { get; }
    internal ExternalFrameSink(NativeHandle handle, VideoFormat format) { this.handle = handle; Format = format; }
    public unsafe bool IsRequestingFrames
    {
        get { using var lease = handle.Borrow(); int value = 0;
            NativeError.Check(NativeMethods.xmax_external_frame_sink_is_requesting_frames(lease.Value, &value)); return value != 0; }
    }
    public unsafe void PushBgra(ReadOnlySpan<byte> pixels, int stride, long timestampMicroseconds)
    {
        using var lease = handle.Borrow();
        fixed (byte* data = pixels)
        {
            var frame = new xmax_video_frame { struct_size = (uint)sizeof(xmax_video_frame), pixel_format = 1,
                width = Format.Width, height = Format.Height, timestamp_us = timestampMicroseconds,
                planes_0 = data, strides_0 = checked((nuint)stride), plane_sizes_0 = (nuint)pixels.Length };
            NativeError.Check(NativeMethods.xmax_external_frame_sink_push_video_frame(lease.Value, &frame));
        }
    }
    public unsafe void PushAudio(ReadOnlySpan<short> samples, uint sampleRate, uint channels, long timestampMicroseconds)
    {
        if (channels == 0 || (uint)samples.Length % channels != 0) throw new ArgumentException("PCM must contain complete interleaved samples.");
        using var lease = handle.Borrow();
        fixed (short* data = samples)
        {
            var frame = new xmax_external_audio_frame { struct_size = (uint)sizeof(xmax_external_audio_frame), sample_format = 0,
                sample_rate = sampleRate, channels = channels, samples_per_channel = (uint)samples.Length / channels,
                timestamp_us = timestampMicroseconds, data = data, data_size = checked((nuint)samples.Length * 2) };
            NativeError.Check(NativeMethods.xmax_external_frame_sink_push_audio_frame(lease.Value, &frame));
        }
    }
    public unsafe void PushNv12(ReadOnlySpan<byte> y, int yStride, ReadOnlySpan<byte> uv, int uvStride, long timestampMicroseconds)
    {
        using var lease = handle.Borrow();
        fixed (byte* yp = y, uvp = uv)
        {
            var frame = new xmax_video_frame { struct_size = (uint)sizeof(xmax_video_frame), pixel_format = 2,
                width = Format.Width, height = Format.Height, timestamp_us = timestampMicroseconds,
                planes_0 = yp, strides_0 = checked((nuint)yStride), plane_sizes_0 = (nuint)y.Length,
                planes_1 = uvp, strides_1 = checked((nuint)uvStride), plane_sizes_1 = (nuint)uv.Length };
            NativeError.Check(NativeMethods.xmax_external_frame_sink_push_video_frame(lease.Value, &frame));
        }
    }
    public unsafe void PushI420(ReadOnlySpan<byte> y, int yStride, ReadOnlySpan<byte> u, int uStride,
        ReadOnlySpan<byte> v, int vStride, long timestampMicroseconds)
    {
        using var lease = handle.Borrow();
        fixed (byte* yp = y, up = u, vp = v)
        {
            var frame = new xmax_video_frame { struct_size = (uint)sizeof(xmax_video_frame), pixel_format = 3,
                width = Format.Width, height = Format.Height, timestamp_us = timestampMicroseconds,
                planes_0 = yp, strides_0 = checked((nuint)yStride), plane_sizes_0 = (nuint)y.Length,
                planes_1 = up, strides_1 = checked((nuint)uStride), plane_sizes_1 = (nuint)u.Length,
                planes_2 = vp, strides_2 = checked((nuint)vStride), plane_sizes_2 = (nuint)v.Length };
            NativeError.Check(NativeMethods.xmax_external_frame_sink_push_video_frame(lease.Value, &frame));
        }
    }
    public unsafe void PushAudio(ReadOnlySpan<float> samples, uint sampleRate, uint channels, long timestampMicroseconds)
    {
        if (channels == 0 || (uint)samples.Length % channels != 0) throw new ArgumentException("PCM must contain complete interleaved samples.");
        using var lease = handle.Borrow();
        fixed (float* data = samples)
        {
            var frame = new xmax_external_audio_frame { struct_size = (uint)sizeof(xmax_external_audio_frame), sample_format = 1,
                sample_rate = sampleRate, channels = channels, samples_per_channel = (uint)samples.Length / channels,
                timestamp_us = timestampMicroseconds, data = data, data_size = checked((nuint)samples.Length * 4) };
            NativeError.Check(NativeMethods.xmax_external_frame_sink_push_audio_frame(lease.Value, &frame));
        }
    }
    public unsafe void ReportError(int code, string message)
    {
        using var lease = handle.Borrow();
        var error = new xmax_error_info { struct_size = (uint)sizeof(xmax_error_info), code = code };
        byte[] text = System.Text.Encoding.UTF8.GetBytes(message);
        int count = Math.Min(text.Length, 1023);
        while (count < text.Length && count > 0 && (text[count] & 0xc0) == 0x80) --count;
        text.AsSpan(0, count).CopyTo(new Span<byte>(error.message, 1024));
        NativeError.Check(NativeMethods.xmax_external_frame_sink_report_error(lease.Value, &error));
    }
    public void Dispose() => handle.Dispose();
}
