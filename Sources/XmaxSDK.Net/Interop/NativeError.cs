using System.Runtime.InteropServices;

namespace Xmax.SDK.Interop;

internal static unsafe class NativeError
{
    internal static string Text(byte* value) => Marshal.PtrToStringUTF8((nint)value) ?? "";
    internal static void Check(int result)
    {
        if (result >= 0) return;
        // Capture immediately on the calling thread: all other SDK calls can overwrite TLS diagnostics.
        var detail = new xmax_error_info { struct_size = (uint)sizeof(xmax_error_info) };
        NativeMethods.xmax_last_error_info(&detail);
        throw new XmaxException(result, Text(detail.message), detail.api_code, detail.http_status);
    }
}

internal sealed unsafe class Utf8 : IDisposable
{
    internal byte* Pointer { get; private set; }
    internal Utf8(string? value) => Pointer = value is null ? null : (byte*)Marshal.StringToCoTaskMemUTF8(value);
    public void Dispose() { Marshal.FreeCoTaskMem((nint)Pointer); Pointer = null; }
}
