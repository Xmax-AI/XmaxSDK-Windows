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

/// <summary>
/// The lifecycle state of a realtime controller.
/// </summary>
public enum RealtimeState
{

    /// <summary>
    /// No local media is prepared.
    /// </summary>
    Idle,

    /// <summary>
    /// Local media is prepared without an active connection.
    /// </summary>
    Ready,

    /// <summary>
    /// A server session and RTC connection are being established.
    /// </summary>
    Connecting,

    /// <summary>
    /// The RTC connection is ready without active generation.
    /// </summary>
    Connected,

    /// <summary>
    /// Generation is active or waiting for its first result.
    /// </summary>
    Generating,

    /// <summary>
    /// The connection is being closed.
    /// </summary>
    Disconnecting,

    /// <summary>
    /// Local media is being prepared or reconfigured.
    /// </summary>
    Preparing
}
