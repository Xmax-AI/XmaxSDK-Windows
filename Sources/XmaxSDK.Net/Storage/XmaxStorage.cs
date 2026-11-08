//
//  XmaxStorage.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;

/// <summary>
/// An owner of cloud upload and download operations with progress and cancellation.
/// </summary>
/// <remarks>
/// Typed image and video methods support concurrent transfers with per-operation progress and cancellation.
/// Await operations instead of blocking the UI thread; native failures are reported as XmaxException.
/// </remarks>
internal sealed partial class XmaxStorage : IXmaxStorageManaging
{
    private readonly OperationTracker operations;

    internal XmaxStorage(NativeHandle handle) => operations = new(handle);

    /// <summary>
    /// Cancels and drains in-flight storage work before releasing the native controller.
    /// </summary>
    /// <returns>An awaitable operation completing after cleanup.</returns>
    /// <remarks>
    /// New operations are rejected once disposal starts.
    /// </remarks>
    public ValueTask DisposeAsync() => new(operations.DisposeAsync(NativeMethods.xmax_storage_cancel));
}
