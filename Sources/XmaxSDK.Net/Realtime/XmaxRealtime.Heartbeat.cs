//
//  XmaxRealtime.Heartbeat.cs
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

internal sealed partial class XmaxRealtime
{
    /// <inheritdoc/>
    public Task SetHeartbeatListenerAsync(Action<string>? listener) =>
        Run(realtime => RegisterHeartbeatListener(realtime, listener));

    private static unsafe void RegisterHeartbeatListener(nint realtime, Action<string>? listener)
    {
        if (listener is null)
        {
            NativeError.Check(NativeMethods.xmax_realtime_set_heartbeat_listener(realtime, null));
            return;
        }
        var handle = GCHandle.Alloc(listener);
        var native = new xmax_heartbeat_listener
        {
            struct_size = (uint)sizeof(xmax_heartbeat_listener),
            user_data = (void*)GCHandle.ToIntPtr(handle),
            on_heartbeat = (nint)(delegate* unmanaged[Cdecl]<void*, byte*, void>)&ReceiveHeartbeat,
            release_user_data = (nint)(delegate* unmanaged[Cdecl]<void*, void>)&ReleaseHeartbeatListener
        };
        try { NativeError.Check(NativeMethods.xmax_realtime_set_heartbeat_listener(realtime, &native)); }
        catch { handle.Free(); throw; }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void ReceiveHeartbeat(void* data, byte* sessionId)
    {
        try
        {
            var listener = (Action<string>)GCHandle.FromIntPtr((nint)data).Target!;
            listener(NativeError.Text(sessionId));
        }
        catch (Exception error) { NotifyCallbackFault(error); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void ReleaseHeartbeatListener(void* data) =>
        GCHandle.FromIntPtr((nint)data).Free();
}
