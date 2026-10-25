//
//  ExternalFrameSink.cs
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
/// A source-bound receiver for caller-owned video and optional PCM capture.
/// </summary>
/// <remarks>
/// Submission copies input before returning. Replaced or closed bindings ignore subsequent input.
/// Your app owns capture, permissions, pacing, and capture shutdown; disposing this receiver does not unbind the source.
/// </remarks>
public sealed class ExternalFrameSink : IDisposable
{
    private readonly NativeHandle handle;

    /// <summary>
    /// Gets the exact source format retained at binding, including after the binding becomes inactive.
    /// </summary>
    public VideoFormat Format { get; }

    internal ExternalFrameSink(NativeHandle handle, VideoFormat format) { this.handle = handle; Format = format; }

    /// <summary>
    /// Gets whether this binding still accepts input, including preview frames while disconnected.
    /// </summary>
    public unsafe bool IsRequestingFrames
    {
        get
        {
            using var lease = handle.Borrow();
            int value = 0;
            NativeError.Check(NativeMethods.xmax_external_frame_sink_is_requesting_frames(lease.Value, &value));
            return value != 0;
        }
    }

    /// <summary>
    /// Copies a source-sized BGRA frame for preview or publication.
    /// </summary>
    /// <param name="pixels">The accessible bytes of the packed BGRA plane; do not modify them during this call.</param>
    /// <param name="stride">The row stride in bytes, including any row padding.</param>
    /// <param name="timestampMicroseconds">A nonnegative, strictly increasing timestamp in monotonic microseconds for this binding; audio uses the same clock.</param>
    /// <remarks>
    /// Invalid pixels on the current binding fail the source and schedule connection/media cleanup. Negative, repeated, or decreasing timestamps are ignored.
    /// </remarks>
    public unsafe void PushBgra(
        ReadOnlySpan<byte> pixels,
        int stride,
        long timestampMicroseconds
    )
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

    /// <summary>
    /// Copies interleaved PCM16 samples for source-bound audio publication.
    /// </summary>
    /// <param name="samples">The interleaved samples for complete channel frames; each sample is signed 16-bit PCM.</param>
    /// <param name="sampleRate">The sample rate, from 8000 through 192000 Hz, fixed for this binding.</param>
    /// <param name="channels">The channel count, from 1 through 8, fixed for this binding.</param>
    /// <param name="timestampMicroseconds">The first sample time, in nonnegative monotonic microseconds on the video clock.</param>
    /// <remarks>
    /// Bind with hasAudio enabled. Submit at most one second per call on one serial audio queue.
    /// The SDK converts to 48 kHz mono PCM16 without local playback and uploads only while published; no audio is queued for a future connection.
    /// Overlapping or negative times are ignored. Format changes fail the binding; input without audio opt-in or on a stale binding is ignored.
    /// </remarks>
    public unsafe void PushAudio(
        ReadOnlySpan<short> samples,
        uint sampleRate,
        uint channels,
        long timestampMicroseconds
    )
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

    /// <summary>
    /// Copies source-sized NV12 luma and interleaved chroma planes.
    /// </summary>
    /// <param name="y">The accessible bytes of the full-resolution luma plane.</param>
    /// <param name="yStride">The luma row stride, in bytes.</param>
    /// <param name="uv">The accessible bytes of the interleaved UV chroma plane.</param>
    /// <param name="uvStride">The chroma row stride, in bytes.</param>
    /// <param name="timestampMicroseconds">A nonnegative, strictly increasing timestamp in monotonic microseconds for this binding; audio uses the same clock.</param>
    /// <remarks>
    /// Planes must cover Format dimensions and must not change during the call. Timestamp and source-failure rules are the same as PushBgra.
    /// </remarks>
    public unsafe void PushNv12(
        ReadOnlySpan<byte> y,
        int yStride,
        ReadOnlySpan<byte> uv,
        int uvStride,
        long timestampMicroseconds
    )
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

    /// <summary>
    /// Copies source-sized I420 luma, U, and V planes.
    /// </summary>
    /// <param name="y">The accessible bytes of the full-resolution luma plane.</param>
    /// <param name="yStride">The luma row stride, in bytes.</param>
    /// <param name="u">The accessible bytes of the U chroma plane.</param>
    /// <param name="uStride">The U-plane row stride, in bytes.</param>
    /// <param name="v">The accessible bytes of the V chroma plane.</param>
    /// <param name="vStride">The V-plane row stride, in bytes.</param>
    /// <param name="timestampMicroseconds">A nonnegative, strictly increasing timestamp in monotonic microseconds for this binding; audio uses the same clock.</param>
    /// <remarks>
    /// Planes must cover Format dimensions and must not change during the call. Timestamp and source-failure rules are the same as PushBgra.
    /// </remarks>
    public unsafe void PushI420(
        ReadOnlySpan<byte> y,
        int yStride,
        ReadOnlySpan<byte> u,
        int uStride,
        ReadOnlySpan<byte> v,
        int vStride,
        long timestampMicroseconds
    )
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

    /// <summary>
    /// Copies interleaved float32 PCM samples for source-bound audio publication.
    /// </summary>
    /// <param name="samples">The interleaved samples for complete channel frames; float values must be finite and within [-1, 1].</param>
    /// <param name="sampleRate">The sample rate, from 8000 through 192000 Hz, fixed for this binding.</param>
    /// <param name="channels">The channel count, from 1 through 8, fixed for this binding.</param>
    /// <param name="timestampMicroseconds">The first sample time, in nonnegative monotonic microseconds on the video clock.</param>
    /// <remarks>
    /// Bind with hasAudio enabled. Submit at most one second per call on one serial audio queue.
    /// The SDK converts to 48 kHz mono PCM16 without local playback and uploads only while published; no audio is queued for a future connection.
    /// Overlapping or negative times are ignored. Format changes fail the binding; input without audio opt-in or on a stale binding is ignored.
    /// </remarks>
    public unsafe void PushAudio(
        ReadOnlySpan<float> samples,
        uint sampleRate,
        uint channels,
        long timestampMicroseconds
    )
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

    /// <summary>
    /// Reports an unrecoverable capture-source failure and schedules media and connection cleanup.
    /// </summary>
    /// <param name="code">A negative SDK result code describing the source failure.</param>
    /// <param name="message">The diagnostic text, copied and truncated at a UTF-8 boundary if needed.</param>
    /// <remarks>
    /// The call does not wait for cleanup. Observe realtime events for completion. Stale and repeated reports are ignored.
    /// </remarks>
    public unsafe void ReportError(
        int code,
        string message
    )
    {
        using var lease = handle.Borrow();
        var error = new xmax_error_info { struct_size = (uint)sizeof(xmax_error_info), code = code };
        byte[] text = System.Text.Encoding.UTF8.GetBytes(message);
        int count = Math.Min(text.Length, 1023);
        while (count < text.Length && count > 0 && (text[count] & 0xc0) == 0x80) --count;
        text.AsSpan(0, count).CopyTo(new Span<byte>(error.message, 1024));
        NativeError.Check(NativeMethods.xmax_external_frame_sink_report_error(lease.Value, &error));
    }

    /// <summary>
    /// Releases the receiver without stopping caller-owned capture or unbinding the source.
    /// </summary>
    /// <remarks>
    /// Stop submitting and wait for in-flight calls before disposal.
    /// </remarks>
    public void Dispose() => handle.Dispose();
}
