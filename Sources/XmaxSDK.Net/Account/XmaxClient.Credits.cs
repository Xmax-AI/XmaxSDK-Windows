//
//  XmaxClient.Credits.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xmax.SDK.Interop;

namespace Xmax.SDK;

public sealed partial class XmaxClient
{
    /// <summary>Fetches current account credits using this client's API key and general service environment.</summary>
    /// <param name="cancellationToken">Cooperatively cancels the request; an active native HTTP phase may take until its configured timeout to return.</param>
    /// <returns>Server-confirmed nonnegative credits, including fractional credits.</returns>
    /// <remarks>
    /// Each invocation queries GET /balance without caching or starting capture, RTC, or generation.
    /// The app controls refresh scheduling and insufficient-credit policy. Request and malformed-response
    /// failures throw XmaxException, preserving service/HTTP error details; they never return zero credits.
    /// An in-flight request retains its native client even if Dispose is called.
    /// </remarks>
    public Task<XmaxCreditBalance> GetCreditBalanceAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var lease = handle.Borrow();
        try {
            return Task.Run(() => { using (lease) return ReadCreditBalance(lease.Value, cancellationToken); });
        } catch { lease.Dispose(); throw; }
    }

    private static unsafe XmaxCreditBalance ReadCreditBalance(nint client, CancellationToken token)
    {
        GCHandle context = default;
        try {
            token.ThrowIfCancellationRequested();
            var cancellation = new xmax_request_cancellation { struct_size = (uint)sizeof(xmax_request_cancellation) };
            if (token.CanBeCanceled) {
                context = GCHandle.Alloc(token);
                cancellation.user_data = (void*)GCHandle.ToIntPtr(context);
                cancellation.is_cancelled = (nint)(delegate* unmanaged[Cdecl]<void*, int>)&IsCreditQueryCancelled;
            }
            var balance = new xmax_credit_balance { struct_size = (uint)sizeof(xmax_credit_balance) };
            NativeError.Check(NativeMethods.xmax_client_get_credit_balance(client, &balance, token.CanBeCanceled ? &cancellation : null));
            token.ThrowIfCancellationRequested();
            return new(balance.current_balance);
        } catch (Exception error) when (token.IsCancellationRequested) {
            throw new OperationCanceledException("Credit balance query cancelled.", error, token);
        } finally { if (context.IsAllocated) context.Free(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe int IsCreditQueryCancelled(void* context)
    {
        try { return ((CancellationToken)GCHandle.FromIntPtr((nint)context).Target!).IsCancellationRequested ? 1 : 0; }
        catch { return 1; }
    }
}
