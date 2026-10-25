//
//  TransferProgress.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// A nonblocking snapshot of the current or most recent storage transfer.
/// </summary>
/// <param name="State">The native transfer state: 0 idle, 1 running, 2 succeeded, 3 failed, or 4 cancelled.</param>
/// <param name="Completed">The number of transferred bytes.</param>
/// <param name="Total">The total byte count; 0 means the total is unknown.</param>
public sealed record TransferProgress(
    int State,
    ulong Completed,
    ulong Total
);
