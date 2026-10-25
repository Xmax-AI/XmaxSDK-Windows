using Xmax.SDK.Interop;

namespace Xmax.SDK;

public sealed record VideoStatistics(bool Available, int Direction, string UserId, int Width, int Height,
    int CaptureFramesPerSecond, int EncodeFramesPerSecond, int SendFramesPerSecond, int DecodeFramesPerSecond,
    int RenderFramesPerSecond, int BitrateKbps, int PacketLossPercent, int EndToEndMilliseconds);

public sealed partial class XmaxRealtime
{
    public unsafe VideoStatistics GetVideoStatistics(int direction)
    {
        using var lease = handle.Borrow();
        var value = new xmax_video_statistics { struct_size = (uint)sizeof(xmax_video_statistics) };
        NativeError.Check(NativeMethods.xmax_realtime_get_video_statistics(lease.Value, direction, &value));
        return new(value.available != 0, value.direction, NativeError.Text(value.user_id), value.width, value.height,
            value.capture_fps, value.encode_fps, value.send_fps, value.decode_fps, value.render_fps, value.bitrate_kbps,
            value.packet_loss_percent, value.e2e_ms);
    }
    public Task<VideoFormat> GetFormatAsync() => operations.Run(GetFormat);
    private static unsafe VideoFormat GetFormat(nint value)
    { xmax_video_format format; NativeError.Check(NativeMethods.xmax_realtime_get_format(value, &format)); return VideoFormat.From(format); }
    public Task<float> GetRemoteVolumeAsync() => operations.Run(GetVolume);
    private static unsafe float GetVolume(nint value)
    { float volume; NativeError.Check(NativeMethods.xmax_realtime_get_remote_volume(value, &volume)); return volume; }
}
