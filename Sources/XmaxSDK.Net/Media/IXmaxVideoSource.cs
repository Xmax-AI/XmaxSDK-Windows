//
//  IXmaxVideoSource.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// A single-consumer mailbox retaining only the latest unconsumed video frame.
/// </summary>
/// <remarks>
/// Each acquired frame has independent ownership. Do not attach multiple independent consumers to the same source; distribute frames explicitly when needed.
/// </remarks>
public interface IXmaxVideoSource
{

    /// <summary>
    /// Takes the latest available frame without waiting.
    /// </summary>
    /// <returns>An owned frame to dispose after use, or null when no frame is available.</returns>
    XmaxVideoFrame? TryAcquireFrame();
}
