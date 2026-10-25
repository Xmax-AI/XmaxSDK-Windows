using Xmax.SDK.Interop;

namespace Xmax.SDK;

public static class XmaxRuntime
{
    /// <summary>Call before creating SDK objects to select a matching native SDK and its dependencies.</summary>
    public static void ConfigureNativeDirectory(string directory) => NativeLibraryLoader.Configure(directory);
    public static unsafe string Version { get { NativeLibraryLoader.Initialize(); return NativeError.Text(NativeMethods.xmax_version()); } }
}
