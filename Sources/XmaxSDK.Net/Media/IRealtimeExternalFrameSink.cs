//
//  IRealtimeExternalFrameSink.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Receives frames and runtime failures from one application-owned capture source.</summary>
/// <remarks>Invalid media fails the attachment and is reported through realtime state.
/// Stale attachments discard submissions. Submit video and audio on serial queues using one monotonic clock.</remarks>
public interface IRealtimeExternalFrameSink
{
    /// <summary>Gets the dimensions and rate required of the source frames.</summary>
    VideoFormat Format { get; }

    /// <summary>Gets the processed preview and publication format.</summary>
    VideoFormat VideoFormat { get; }

    /// <summary>Gets whether this attachment still accepts preview and publication frames.</summary>
    bool IsRequestingFrames { get; }

    /// <summary>Copies a source-sized BGRA frame with the given byte stride and monotonic timestamp.</summary>
    void PushBgra(ReadOnlySpan<byte> pixels, int stride, long timestampMicroseconds);

    /// <summary>Copies source-sized NV12 luma and interleaved chroma planes with byte strides.</summary>
    void PushNv12(ReadOnlySpan<byte> y, int yStride, ReadOnlySpan<byte> uv, int uvStride, long timestampMicroseconds);

    /// <summary>Copies source-sized I420 luma and chroma planes with byte strides.</summary>
    void PushI420(ReadOnlySpan<byte> y, int yStride, ReadOnlySpan<byte> u, int uStride,
        ReadOnlySpan<byte> v, int vStride, long timestampMicroseconds);

    /// <summary>Copies interleaved signed PCM16 samples on the source clock.</summary>
    void PushAudio(ReadOnlySpan<short> samples, uint sampleRate, uint channels, long timestampMicroseconds);

    /// <summary>Copies interleaved float32 PCM samples on the source clock.</summary>
    void PushAudio(ReadOnlySpan<float> samples, uint sampleRate, uint channels, long timestampMicroseconds);

    /// <summary>Copies little-endian linear PCM. Planar input concatenates complete channel planes.</summary>
    void PushAudio(ReadOnlySpan<byte> data, ExternalAudioSampleFormat format, uint sampleRate, uint channels,
        long timestampMicroseconds);

    /// <summary>Fails this attachment and reports the source exception as an SDK media error.</summary>
    void ReportError(Exception error);
}
