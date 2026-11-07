//
//  RealtimeState.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The current realtime connection state and its optional business identifiers and reason.</summary>
/// <param name="ConnectionState">The connection lifecycle phase.</param>
/// <param name="SessionId">The current or most recent session identifier.</param>
/// <param name="TaskId">The current generation task identifier.</param>
/// <param name="Reason">The reason for the state, when applicable.</param>
public sealed record RealtimeState(
    RealtimeConnectionState ConnectionState,
    string? SessionId = null,
    string? TaskId = null,
    RealtimeReason? Reason = null
)
{
    internal static RealtimeState From(RealtimeEvent item) => new(
        item.State,
        item.SessionId.Length == 0 ? null : item.SessionId,
        item.TaskId.Length == 0 ? null : item.TaskId,
        item.Reason switch
        {
            0 => RealtimeReason.Normal,
            1 => RealtimeReason.OrientationChanged,
            2 => new RealtimeReason.Failure(new(item.Code, item.Message, item.ApiCode, item.HttpStatus)),
            _ => null
        }
    );
}
