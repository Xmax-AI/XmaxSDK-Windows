using System.Runtime.InteropServices;

// A minimal C ABI consumer, also usable as a WPF P/Invoke reference. No camera/network is used.
internal static unsafe class Program
{
    private const string Dll = "XmaxSDK";
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct Config { public uint Size, Abi; public nint Key, Model; public uint Timeout; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct Capabilities { public uint Size, Abi; public ulong Flags; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct VideoFormat { public uint Width, Height, Fps; }
    [StructLayout(LayoutKind.Sequential, Pack = 8)]
    private struct Event
    {
        public uint Size; public int Type, State, Code;
        public fixed byte Message[512]; public fixed byte SessionId[128]; public fixed byte TaskId[128];
        public int Direction, Width, Height, Fps, Bitrate, PacketLoss, Quality, E2e;
        public double SessionMs, JoinMs, PublishMs, FirstFrameMs, TotalMs;
    }
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern nint xmax_version();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern nint xmax_last_error();
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern int xmax_client_create(in Config config, out nint client);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern void xmax_client_destroy(nint client);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern int xmax_client_get_capabilities(nint client, ref Capabilities caps);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern int xmax_realtime_create(nint client, out nint realtime);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern int xmax_realtime_prepare_external(nint realtime, in VideoFormat format);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern int xmax_realtime_get_state(nint realtime, ref Event state);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern int xmax_realtime_close(nint realtime);
    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)] private static extern void xmax_realtime_destroy(nint realtime);
    private static void Check(int result)
    {
        if (result < 0) throw new InvalidOperationException(Marshal.PtrToStringUTF8(xmax_last_error()));
    }
    public static int Main()
    {
        nint client = 0, realtime = 0;
        var key = Marshal.StringToCoTaskMemUTF8("");
        try
        {
            if (IntPtr.Size != 8 || sizeof(Config) != 32 || sizeof(Event) != 856 || sizeof(Capabilities) != 16)
                throw new InvalidOperationException("C ABI layout mismatch");
            var config = new Config { Size = (uint)sizeof(Config), Abi = 1, Key = key, Timeout = 15000 };
            Check(xmax_client_create(in config, out client));
            var caps = new Capabilities { Size = (uint)sizeof(Capabilities) };
            Check(xmax_client_get_capabilities(client, ref caps));
            if (caps.Abi != 1 || (caps.Flags & 8) == 0) throw new InvalidOperationException("Capability mismatch");
            Check(xmax_realtime_create(client, out realtime));
            xmax_client_destroy(client); client = 0; // Child ownership is independent.
            var format = new VideoFormat { Width = 1920, Height = 1024, Fps = 30 };
            Check(xmax_realtime_prepare_external(realtime, in format));
            var state = new Event { Size = (uint)sizeof(Event) };
            Check(xmax_realtime_get_state(realtime, ref state));
            if (state.State != 1) throw new InvalidOperationException("Expected READY");
            Check(xmax_realtime_close(realtime));
            Console.WriteLine($"P/Invoke consumer passed; SDK {Marshal.PtrToStringUTF8(xmax_version())}, caps={caps.Flags}");
            return 0;
        }
        finally
        {
            xmax_realtime_destroy(realtime); xmax_client_destroy(client); Marshal.FreeCoTaskMem(key);
        }
    }
}
