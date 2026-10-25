//
//  XmaxLogger.cs
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
/// Configures and reads process-wide SDK diagnostics shared by all clients.
/// </summary>
/// <remarks>
/// Logging is disabled by default. Client creation applies its configured mask and environment language.
/// Assign a single consumer to the bounded queue; logs are diagnostic output, not a business-state source.
/// </remarks>
public static class XmaxLogger
{

    /// <summary>
    /// Sets process-wide diagnostic categories and language, clearing queued records.
    /// </summary>
    /// <param name="options">The Business and Performance bitmask; None disables logging.</param>
    /// <param name="language">The diagnostic language; the default is English.</param>
    public static void Configure(
        LogOptions options,
        LogLanguage language = LogLanguage.English
    )
    {
        NativeLibraryLoader.Initialize();
        NativeError.Check(NativeMethods.xmax_logger_configure((uint)options, (int)language));
    }

    /// <summary>
    /// Removes the oldest queued diagnostic record without waiting.
    /// </summary>
    /// <returns>A copied log record, or null when the queue is empty.</returns>
    /// <remarks>
    /// Unread old records may be discarded when the bounded queue fills. Sequence gaps can indicate lost records.
    /// </remarks>
    public static unsafe LogRecord? TryRead()
    {
        NativeLibraryLoader.Initialize();
        var value = new xmax_log_record { struct_size = (uint)sizeof(xmax_log_record) };
        int result = NativeMethods.xmax_logger_poll(&value);
        NativeError.Check(result);
        return result == 1 ? null : new(value.level, NativeError.Text(value.category), NativeError.Text(value.message), value.timestamp_us, value.sequence);
    }
}
