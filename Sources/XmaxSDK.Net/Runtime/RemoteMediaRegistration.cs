using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xmax.SDK.Interop;

namespace Xmax.SDK.Runtime;

internal static unsafe class RemoteMediaRegistration
{
    private sealed record Context(Action<XmaxMediaFrame> Receive, Action<Exception> Failure);

    internal static void Set(nint realtime, Action<XmaxMediaFrame>? listener, Action<Exception> failure)
    {
        if (listener is null)
        { NativeError.Check(NativeMethods.xmax_realtime_set_remote_media_frame_listener(realtime, null)); return; }
        var context = GCHandle.Alloc(new Context(listener, failure));
        var native = new xmax_remote_media_listener
        {
            struct_size = (uint)sizeof(xmax_remote_media_listener), user_data = (void*)GCHandle.ToIntPtr(context),
            on_frame = (nint)(delegate* unmanaged[Cdecl]<void*, nint, void>)&Receive,
            release_user_data = (nint)(delegate* unmanaged[Cdecl]<void*, void>)&Release
        };
        try { NativeError.Check(NativeMethods.xmax_realtime_set_remote_media_frame_listener(realtime, &native)); }
        catch { context.Free(); throw; }
        // Ownership transfers to native release_user_data only after registration succeeds.
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Receive(void* userData, nint borrowed)
    {
        Context? context = null;
        XmaxMediaFrame? packet = null;
        try
        {
            context = (Context)GCHandle.FromIntPtr((nint)userData).Target!;
            nint retained = 0;
            NativeError.Check(NativeMethods.xmax_media_frame_retain(borrowed, &retained));
            packet = new(retained);
            context.Receive(packet);
            packet = null; // The application now owns this retained packet.
        }
        catch (Exception error) { try { context?.Failure(error); } catch { } }
        finally { packet?.Dispose(); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Release(void* userData)
    { try { GCHandle.FromIntPtr((nint)userData).Free(); } catch { /* never cross a native callback boundary */ } }
}
