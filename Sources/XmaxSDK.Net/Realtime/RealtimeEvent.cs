using Xmax.SDK.Interop;

namespace Xmax.SDK;

public sealed record RealtimeEvent(RealtimeEventType Type, RealtimeState State, int Code, string Message,
    string SessionId, string TaskId, int Reason, int ApiCode, int HttpStatus,
    int Direction, int Width, int Height, int FramesPerSecond, int BitrateKbps, int PacketLossPercent,
    int Quality, int EndToEndMilliseconds, double SessionMilliseconds, double JoinMilliseconds,
    double PublishMilliseconds, double FirstFrameMilliseconds, double TotalMilliseconds)
{
    internal static unsafe RealtimeEvent From(xmax_event_info info)
    {
        var e = info.@event;
        return new((RealtimeEventType)e.type, (RealtimeState)e.state, e.code,
            NativeError.Text(e.message), NativeError.Text(e.session_id), NativeError.Text(e.task_id),
            info.reason, info.error.api_code, info.error.http_status, e.direction, e.width, e.height, e.fps,
            e.bitrate_kbps, e.packet_loss_percent, e.quality, e.e2e_ms, e.session_ms, e.rtc_join_ms,
            e.publish_ms, e.first_frame_ms, e.total_ms);
    }
}
