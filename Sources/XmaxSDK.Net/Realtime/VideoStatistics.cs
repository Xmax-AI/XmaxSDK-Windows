//
//  VideoStatistics.cs
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
/// A directional video snapshot separating capture, codec, transport, and render measurements.
/// </summary>
/// <param name="Available">Whether statistics are available for the current connection.</param>
/// <param name="Direction">The transport direction: 0 uplink or 1 downlink.</param>
/// <param name="UserId">The result-user identifier for downlink; empty for uplink or when unavailable.</param>
/// <param name="Width">The encoded or decoded video width, in pixels.</param>
/// <param name="Height">The encoded or decoded video height, in pixels.</param>
/// <param name="CaptureFramesPerSecond">The source capture rate; applicable to uplink.</param>
/// <param name="EncodeFramesPerSecond">The encoder output rate; applicable to uplink.</param>
/// <param name="SendFramesPerSecond">The outgoing transmission rate; applicable to uplink.</param>
/// <param name="DecodeFramesPerSecond">The decoder output rate; applicable to downlink.</param>
/// <param name="RenderFramesPerSecond">The native RTC render rate before SDK presentation; applicable to downlink.</param>
/// <param name="BitrateKbps">The sent or received video bitrate, in kbps.</param>
/// <param name="PacketLossPercent">The sent or received packet-loss rate, from 0 through 100 percent.</param>
/// <param name="EndToEndMilliseconds">The RTC media end-to-end delay, in milliseconds; applicable to downlink.</param>
/// <remarks>
/// Unavailable numeric measurements are -1. Capture, encoding, sending, decoding, and rendering rates have different scopes.
/// </remarks>
public sealed record VideoStatistics(
    bool Available,
    int Direction,
    string UserId,
    int Width,
    int Height,
    int CaptureFramesPerSecond,
    int EncodeFramesPerSecond,
    int SendFramesPerSecond,
    int DecodeFramesPerSecond,
    int RenderFramesPerSecond,
    int BitrateKbps,
    int PacketLossPercent,
    int EndToEndMilliseconds
);

public sealed partial class XmaxRealtime
{

    /// <summary>
    /// Reads directional RTC measurements independently of event consumption.
    /// </summary>
    /// <param name="direction">The transport direction: 0 uplink or 1 downlink.</param>
    /// <returns>The latest measurements; Available is false before statistics arrive or after disconnection.</returns>
    public unsafe VideoStatistics GetVideoStatistics(
        int direction
    )
    {
        using var lease = handle.Borrow();
        var value = new xmax_video_statistics { struct_size = (uint)sizeof(xmax_video_statistics) };
        NativeError.Check(NativeMethods.xmax_realtime_get_video_statistics(lease.Value, direction, &value));
        return new(value.available != 0, value.direction, NativeError.Text(value.user_id), value.width, value.height,
            value.capture_fps, value.encode_fps, value.send_fps, value.decode_fps, value.render_fps, value.bitrate_kbps,
            value.packet_loss_percent, value.e2e_ms);
    }

    /// <summary>
    /// Reads the current local input or camera encoding format.
    /// </summary>
    /// <returns>A task containing the bound format; camera capture frames may have different dimensions.</returns>
    /// <remarks>
    /// This query may wait for native lifecycle work.
    /// </remarks>
    public Task<VideoFormat> GetFormatAsync() => operations.Run(GetFormat);

    private static unsafe VideoFormat GetFormat(nint value)
    { xmax_video_format format; NativeError.Check(NativeMethods.xmax_realtime_get_format(value, &format)); return VideoFormat.From(format); }

    /// <summary>
    /// Reads the playback volume retained for the current or next connection.
    /// </summary>
    /// <returns>A task containing a volume from 0 through 1, initially 0.</returns>
    /// <remarks>
    /// This query does not wait for native connection or generation admission.
    /// </remarks>
    public Task<float> GetRemoteVolumeAsync() => operations.Run(GetVolume);

    private static unsafe float GetVolume(nint value)
    { float volume; NativeError.Check(NativeMethods.xmax_realtime_get_remote_volume(value, &volume)); return volume; }
}
