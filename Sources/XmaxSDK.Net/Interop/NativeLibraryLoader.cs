using System.Reflection;
using System.Runtime.InteropServices;

namespace Xmax.SDK.Interop;

internal static class NativeLibraryLoader
{
    private static string? directory;
    static NativeLibraryLoader() => NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
    internal static void Initialize()
    {
        if (OperatingSystem.IsWindows() && RuntimeInformation.ProcessArchitecture != Architecture.X64)
            throw new PlatformNotSupportedException("XmaxSDK for Windows requires an x64 process. Set PlatformTarget=x64.");
    }
    internal static void Configure(string path)
    {
        if (!Directory.Exists(path)) throw new DirectoryNotFoundException(path);
        directory = Path.GetFullPath(path);
    }
    private static nint Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != NativeMethods.Library) return 0;
        string file = OperatingSystem.IsWindows() ? "XmaxSDK.dll" : "libXmaxSDK.dylib";
        foreach (var root in new[] { directory, Environment.GetEnvironmentVariable("XMAX_NATIVE_DIR"),
                     Path.Combine(AppContext.BaseDirectory, "runtimes", "win-x64", "native"), AppContext.BaseDirectory })
        {
            if (root is null) continue;
            string path = Path.Combine(root, file);
            if (!File.Exists(path)) continue;
            // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR includes the selected SDK's Agora dependencies.
            nint module = OperatingSystem.IsWindows() ? LoadLibraryEx(path, 0, 0x100 | 0x1000) : NativeLibrary.Load(path);
            if (module != 0) return module;
            throw new DllNotFoundException($"Cannot load {path} and its native dependencies (Win32 {Marshal.GetLastPInvokeError()}).");
        }
        return 0;
    }
    [DllImport("kernel32", EntryPoint = "LoadLibraryExW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint LoadLibraryEx(string path, nint file, uint flags);
}
