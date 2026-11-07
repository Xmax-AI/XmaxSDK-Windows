//
//  RealtimeReason.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The reason for entering a realtime connection state.</summary>
public abstract record RealtimeReason
{
    private RealtimeReason() { }

    /// <summary>An explicit stop or normal resource release.</summary>
    public static RealtimeReason Normal { get; } = new NormalReason();

    /// <summary>A display orientation change that requires reconfiguration.</summary>
    public static RealtimeReason OrientationChanged { get; } = new OrientationChangedReason();

    /// <summary>An operation or runtime failure with its original SDK error.</summary>
    /// <param name="Error">The SDK failure and associated service diagnostics.</param>
    public sealed record Failure(XmaxException Error) : RealtimeReason;

    private sealed record NormalReason : RealtimeReason;
    private sealed record OrientationChangedReason : RealtimeReason;

    internal int DisconnectCode => this switch
    {
        NormalReason => 0,
        OrientationChangedReason => 1,
        _ => throw new ArgumentException("A disconnect reason must be normal or an orientation change.")
    };
}
