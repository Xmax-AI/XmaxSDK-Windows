//
//  XmaxRealtime.Cleanup.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

internal sealed partial class XmaxRealtime
{
    /// <summary>
    /// Disconnects the server session and RTC, clears generation conditions, and retains local preview.
    /// </summary>
    /// <returns>A task completing after disconnection.</returns>
    /// <remarks>
    /// Requests cancellation of in-flight lifecycle work. Prepared local media can be used for reconnection.
    /// Service cleanup failures are logged without replacing the disconnect reason.
    /// </remarks>
    public Task DisconnectAsync() => Run(value => NativeError.Check(NativeMethods.xmax_realtime_disconnect(value)));

    /// <summary>Disconnects for a business reason while retaining local preview.</summary>
    /// <param name="reason">A normal disconnect, an orientation change, or an SDK failure.</param>
    public Task DisconnectAsync(
        RealtimeReason reason
    )
    {
        ArgumentNullException.ThrowIfNull(reason);
        if (reason is RealtimeReason.Failure failure)
            return Run(value => DisconnectWithFailure(value, failure.Error));
        int code = reason.DisconnectCode;
        return Run(value => NativeError.Check(NativeMethods.xmax_realtime_disconnect_with_reason(value, code)));
    }

    private static unsafe void DisconnectWithFailure(nint value, XmaxException failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        using var message = new Utf8(failure.Message);
        NativeError.Check(NativeMethods.xmax_realtime_disconnect_with_error(
            value, failure.Result, message.Pointer, failure.ApiCode, failure.HttpStatus));
    }

    /// <summary>
    /// Disconnects and releases local media and cached generation conditions.
    /// </summary>
    /// <returns>A task completing after native resources are closed.</returns>
    /// <remarks>
    /// The controller remains usable: prepare media again before starting a new session. This method requests cancellation of in-flight startup.
    /// </remarks>
    public async Task CloseAsync()
    {
        BeginExternalClose();
        bool checkLateAdmission = localMediaGate.CurrentCount == 0;
        try
        {
            try { await Run(CloseNative).ConfigureAwait(false); }
            finally { await FinishExternalCloseAsync(checkLateAdmission).ConfigureAwait(false); }
        }
        finally
        {
            lock (externalStartupGate)
                --externalCloseCount;
        }
    }

    private static void CloseNative(nint value) => NativeError.Check(NativeMethods.xmax_realtime_close(value));
}
