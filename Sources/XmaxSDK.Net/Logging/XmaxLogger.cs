using Xmax.SDK.Interop;

namespace Xmax.SDK;

public static class XmaxLogger
{
    public static void Configure(LogOptions options, LogLanguage language = LogLanguage.English)
    {
        NativeLibraryLoader.Initialize();
        NativeError.Check(NativeMethods.xmax_logger_configure((uint)options, (int)language));
    }
    public static unsafe LogRecord? TryRead()
    {
        NativeLibraryLoader.Initialize();
        var value = new xmax_log_record { struct_size = (uint)sizeof(xmax_log_record) };
        int result = NativeMethods.xmax_logger_poll(&value);
        NativeError.Check(result);
        return result == 1 ? null : new(value.level, NativeError.Text(value.category), NativeError.Text(value.message), value.timestamp_us, value.sequence);
    }
}
