//
//  XmaxRealtime.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Diagnostics;
using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;

/// <summary>
/// An owner of local media, a realtime connection, and generation conditions.
/// </summary>
/// <remarks>
/// Await asynchronous operations instead of blocking the UI thread. Native failures are reported as XmaxException.
/// DisposeAsync rejects new work, closes native resources, and drains in-flight calls and events.
/// Do not synchronously wait for disposal inside an SDK event or media callback.
/// </remarks>
internal sealed partial class XmaxRealtime : IXmaxRealtimeManaging
{
    private readonly NativeHandle handle;

    private readonly OperationTracker operations;

    private readonly CancellationTokenSource eventsStop = new();

    private readonly Task eventsWorker;

    private readonly SynchronizationContext? callbackContext;

    private SynchronizationContext? CallbackContext => callbackContext ?? SynchronizationContext.Current;

    /// <summary>Gets the configuration supplied when creating this controller.</summary>
    public RealtimeConfiguration Configuration { get; }

    internal XmaxRealtime(NativeHandle handle, RealtimeConfiguration options, SynchronizationContext? callbackContext)
    {
        this.handle = handle;
        Configuration = options;
        this.callbackContext = callbackContext;
        operations = new(handle);
        stateListener.Publish(new(RealtimeConnectionState.Idle));
        eventsWorker = Task.Run(PumpEventsAsync);
    }

    private Task Run(Action<nint> action) => operations.Run(value =>
    {
        action(value);
        return 0;
    });

    /// <summary>
    /// Reads lifecycle state, identifiers, state reason, and associated error without consuming events.
    /// </summary>
    /// <returns>A task containing the state snapshot.</returns>
    /// <remarks>
    /// The snapshot is available while connection, generation, or cleanup is in progress.
    /// </remarks>
    public Task<RealtimeState> GetStateAsync() => operations.Run(GetState);

    private static unsafe RealtimeState GetState(nint value)
    {
        var native = new xmax_event_info { struct_size = (uint)sizeof(xmax_event_info) };
        NativeError.Check(NativeMethods.xmax_realtime_get_state_info(value, &native));
        return RealtimeState.From(RealtimeEvent.From(native));
    }

    private async Task PumpEventsAsync()
    {
        try
        {
            while (!eventsStop.IsCancellationRequested)
            {
                foreach (var item in PollEvents())
                {
                    if (item.State == RealtimeConnectionState.Idle && Volatile.Read(ref externalAttachment) is not null)
                        _ = ReconcileExternalSourceAsync();
                    try { DispatchTypedEvent(item); } catch (Exception error) { NotifyCallbackFault(error); }
                }
                ApplyStateRegistrations();
                await Task.Delay(25, eventsStop.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (eventsStop.IsCancellationRequested) { }
        catch (Exception error) { NotifyCallbackFault(error); }
        finally { CloseStateRegistrations(); }
    }

    private unsafe List<RealtimeEvent> PollEvents()
    {
        using var lease = handle.Borrow();
        List<RealtimeEvent> batch = [];
        for (int i = 0; i < 256; ++i)
        {
            var native = new xmax_event_info { struct_size = (uint)sizeof(xmax_event_info) };
            int result = NativeMethods.xmax_realtime_poll_event_info(lease.Value, &native);
            NativeError.Check(result);
            if (result == 1) break;
            batch.Add(RealtimeEvent.From(native));
        }
        return batch;
    }

    private static void NotifyCallbackFault(Exception error)
    {
        try { Trace.TraceError("XmaxSDK realtime callback failed: {0}", error.GetType().Name); }
        catch { /* Diagnostic listeners must not interrupt realtime work. */ }
    }

    /// <summary>
    /// Closes this controller, drains in-flight operations and events, and releases its native handle.
    /// </summary>
    /// <returns>An awaitable operation completing after cleanup.</returns>
    /// <remarks>
    /// New operations are rejected once disposal starts. Acquired frame owners remain independently valid.
    /// Do not synchronously wait for this operation inside an SDK event or media callback.
    /// </remarks>
    public ValueTask DisposeAsync()
    {
        BeginExternalClose(disposing: true);
        return new(operations.DisposeAsync(CloseNative, async () =>
        {
            await FinishExternalCloseAsync().ConfigureAwait(false);
            eventsStop.Cancel();
            await eventsWorker.ConfigureAwait(false);
            ClearTypedListeners();
            eventsStop.Dispose();
        }));
    }
}
