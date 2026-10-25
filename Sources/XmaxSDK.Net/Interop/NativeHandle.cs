using Microsoft.Win32.SafeHandles;

namespace Xmax.SDK.Interop;

internal enum HandleKind { Client, Realtime, Storage, Frame, CameraList, UploadedFile, String, ExternalSink, MediaFrame }

internal sealed class NativeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    private readonly HandleKind kind;
    internal NativeHandle(nint value, HandleKind kind) : base(true) { this.kind = kind; SetHandle(value); }
    protected override bool ReleaseHandle()
    {
        switch (kind)
        {
            case HandleKind.Client: NativeMethods.xmax_client_destroy(handle); break;
            case HandleKind.Realtime: NativeMethods.xmax_realtime_destroy(handle); break;
            case HandleKind.Storage: NativeMethods.xmax_storage_destroy(handle); break;
            case HandleKind.Frame: NativeMethods.xmax_frame_release(handle); break;
            case HandleKind.CameraList: NativeMethods.xmax_camera_list_release(handle); break;
            case HandleKind.UploadedFile: NativeMethods.xmax_uploaded_file_release(handle); break;
            case HandleKind.String: NativeMethods.xmax_string_release(handle); break;
            case HandleKind.ExternalSink: NativeMethods.xmax_external_frame_sink_release(handle); break;
            case HandleKind.MediaFrame: NativeMethods.xmax_media_frame_release(handle); break;
        }
        return true;
    }
    internal Lease Borrow() => new(this);
    internal sealed class Lease : IDisposable
    {
        private NativeHandle? owner;
        internal nint Value { get; }
        internal Lease(NativeHandle handle)
        {
            bool added = false;
            handle.DangerousAddRef(ref added);
            owner = handle;
            Value = handle.DangerousGetHandle();
        }
        public void Dispose() => Interlocked.Exchange(ref owner, null)?.DangerousRelease();
    }
}
