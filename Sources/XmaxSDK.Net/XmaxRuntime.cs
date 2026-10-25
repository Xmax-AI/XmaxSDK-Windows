//
//  XmaxRuntime.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>
/// Provides native runtime selection and version information.
/// </summary>
public static class XmaxRuntime
{

    /// <summary>
    /// Selects the directory containing the native SDK and its matching dependencies.
    /// </summary>
    /// <param name="directory">An existing directory containing XmaxSDK.dll and the required native runtime DLLs.</param>
    /// <remarks>
    /// Configure the directory before the first native call. Changing this value does not unload or replace an already loaded DLL.
    /// </remarks>
    /// <exception cref="DirectoryNotFoundException">The directory does not exist.</exception>
    public static void ConfigureNativeDirectory(
        string directory
    ) => NativeLibraryLoader.Configure(directory);

    /// <summary>
    /// Gets the semantic version reported by the loaded native SDK.
    /// </summary>
    public static unsafe string Version
    {
        get
        {
            NativeLibraryLoader.Initialize();
            return NativeError.Text(NativeMethods.xmax_version());
        }
    }
}
