//
//  RealtimeEvent.cs
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
/// A copied realtime event or state snapshot; Type determines which fields are meaningful.
/// </summary>
/// <param name="Type">The event category.</param>
/// <param name="State">The realtime state when the event or snapshot was produced.</param>
/// <param name="Code">The SDK result code; performance events use 1 for device limitation and 0 for recovery.</param>
/// <param name="Message">The diagnostic or status text.</param>
/// <param name="SessionId">The server session identifier, empty before session creation.</param>
/// <param name="TaskId">The generation task identifier, empty before generation starts.</param>
/// <param name="Reason">The state reason: 0 normal, 1 orientation change, or 2 failure.</param>
/// <param name="ApiCode">The service business error code, or 0 when absent.</param>
/// <param name="HttpStatus">The HTTP status code, or 0 when absent.</param>
/// <param name="Direction">The event transport direction: 0 uplink or 1 downlink.</param>
/// <param name="Width">The statistics video width, in pixels.</param>
/// <param name="Height">The statistics video height, in pixels.</param>
/// <param name="FramesPerSecond">The statistics video frame rate, in frames per second.</param>
/// <param name="BitrateKbps">The statistics video bitrate, in kbps.</param>
/// <param name="PacketLossPercent">The statistics video packet-loss percentage.</param>
/// <param name="Quality">The network-quality rating for statistics, or the selected quality level for quality-change events.</param>
/// <param name="EndToEndMilliseconds">The RTC end-to-end latency estimate, in milliseconds.</param>
/// <param name="SessionMilliseconds">The server-session creation duration, in milliseconds.</param>
/// <param name="JoinMilliseconds">The RTC room-join duration, in milliseconds.</param>
/// <param name="PublishMilliseconds">The time until the first actual local stream publication, in milliseconds.</param>
/// <param name="FirstFrameMilliseconds">The time from generation start to its first result frame, in milliseconds.</param>
/// <param name="TotalMilliseconds">The startup request duration through the committed first result, excluding camera preparation and existing-connection idle time.</param>
/// <remarks>
/// Unavailable metrics and unfinished timing stages are -1. A negative Code on timing events reports failed startup with elapsed time.
/// EventReceived consumes the native event queue; GetStateAsync reads a snapshot without consuming events.
/// </remarks>
public sealed record RealtimeEvent(
    RealtimeEventType Type,
    RealtimeState State,
    int Code,
    string Message,
    string SessionId,
    string TaskId,
    int Reason,
    int ApiCode,
    int HttpStatus,
    int Direction,
    int Width,
    int Height,
    int FramesPerSecond,
    int BitrateKbps,
    int PacketLossPercent,
    int Quality,
    int EndToEndMilliseconds,
    double SessionMilliseconds,
    double JoinMilliseconds,
    double PublishMilliseconds,
    double FirstFrameMilliseconds,
    double TotalMilliseconds
)
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
