//
//  XmaxRealtime.Beauty.cs
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
    private long beautyConfigurationRevision;
    /// <summary>Configures result beauty; null disables processing and releases its helper.</summary>
    /// <remarks>
    /// Configure before connecting or during generation. Reconnection retains options; CloseAsync
    /// restores enabled, zero-strength defaults. Invalid options are rejected before changing processing.
    /// Initialization/configuration failures throw here; later frame failures notify the independent listener.
    /// </remarks>
    public Task SetRemoteVideoBeautyAsync(RealtimeVideoBeautyOptions? options)
    {
        options?.Validate();
        return operations.Run(value => { Interlocked.Increment(ref beautyConfigurationRevision); return SetBeauty(value, options); });
    }
    private static unsafe int SetBeauty(nint realtime, RealtimeVideoBeautyOptions? options)
    {
        var native = options?.Native ?? default;
        NativeError.Check(NativeMethods.xmax_realtime_set_remote_video_beauty(realtime, options is null ? null : &native));
        return 0;
    }

    /// <summary>Registers recoverable beauty failures after the SDK restores original-frame output.</summary>
    /// <remarks>
    /// Uses the controller or registering synchronization context; otherwise uses the thread pool.
    /// Callback exceptions are logged. Null unregisters; full close also clears this listener.
    /// A queued notification is discarded when its listener is released. Generation errors use the existing events.
    /// </remarks>
    public Task SetRemoteVideoBeautyErrorListenerAsync(Action<XmaxException>? listener)
    {
        var context = listener is null ? null : new BeautyListener(listener, CallbackContext, NotifyCallbackFault, () => Volatile.Read(ref beautyConfigurationRevision));
        return operations.Run(value => RegisterBeautyListener(value, context));
    }
    private sealed class BeautyListener(Action<XmaxException> callback, SynchronizationContext? context, Action<Exception> failure, Func<long> revision)
    {
        internal int Active = 1;
        internal void Notify(XmaxException error)
        {
            long current = revision();
            void Deliver(object? _) {
                if (Volatile.Read(ref Active) == 0 || current != revision()) return;
                try { callback(error); } catch (Exception fault) { failure(fault); }
            }
            if (context is not null) context.Post(Deliver, null);
            else ThreadPool.QueueUserWorkItem(Deliver);
        }
    }
    private static unsafe int RegisterBeautyListener(nint realtime, BeautyListener? listener)
    {
        if (listener is null) {
            NativeError.Check(NativeMethods.xmax_realtime_set_remote_video_beauty_error_listener(realtime, null)); return 0;
        }
        var context = GCHandle.Alloc(listener);
        var native = new xmax_video_beauty_error_listener {
            struct_size = (uint)sizeof(xmax_video_beauty_error_listener), user_data = (void*)GCHandle.ToIntPtr(context),
            on_error = (nint)(delegate* unmanaged[Cdecl]<void*, xmax_error_info*, void>)&ReceiveBeautyError,
            release_user_data = (nint)(delegate* unmanaged[Cdecl]<void*, void>)&ReleaseBeautyListener
        };
        try { NativeError.Check(NativeMethods.xmax_realtime_set_remote_video_beauty_error_listener(realtime, &native)); }
        catch { context.Free(); throw; }
        return 0;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void ReceiveBeautyError(void* data, xmax_error_info* error)
    {
        try {
            var listener = (BeautyListener)GCHandle.FromIntPtr((nint)data).Target!;
            listener.Notify(new(error->code, NativeError.Text(error->message), error->api_code, error->http_status));
        } catch { /* Never cross the native callback boundary. */ }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void ReleaseBeautyListener(void* data)
    {
        var context = GCHandle.FromIntPtr((nint)data);
        if (context.Target is BeautyListener listener) Volatile.Write(ref listener.Active, 0);
        context.Free();
    }
}
