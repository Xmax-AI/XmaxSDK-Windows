//
//  XmaxClient.cs
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
/// An owner of client configuration for realtime controllers, storage objects and account credit queries.
/// </summary>
/// <remarks>
/// Creating a client starts no capture or network session. Child controllers retain their own configuration
/// and remain usable after this client is disposed. Native failures are reported as XmaxException.
/// </remarks>
public sealed partial class XmaxClient : IDisposable
{
    private readonly NativeHandle handle;

    /// <summary>
    /// Creates a client with copied configuration without starting capture or a cloud session.
    /// </summary>
    /// <param name="configuration">The client settings; null selects the default settings.</param>
    public unsafe XmaxClient(
        XmaxConfiguration? configuration = null
    )
    {
        NativeLibraryLoader.Initialize();
        configuration ??= new();
        using var key = new Utf8(configuration.ApiKey);
        var options = new xmax_client_options
        {
            struct_size = (uint)sizeof(xmax_client_options), abi_version = 2, api_key = key.Pointer,
            environment = (int)configuration.Environment, logger_options = configuration.LoggerOptions,
            http_timeout_ms = configuration.HttpTimeoutMilliseconds
        };
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_client_create_with_options(&options, &value));
        handle = new(value, HandleKind.Client);
    }

    /// <summary>
    /// Queries features implemented by the loaded native SDK build.
    /// </summary>
    /// <returns>The implemented feature flags; camera support does not guarantee a connected device or permission.</returns>
    public unsafe XmaxCapabilities GetCapabilities()
    {
        using var lease = handle.Borrow();
        var value = new xmax_capabilities { struct_size = (uint)sizeof(xmax_capabilities) };
        NativeError.Check(NativeMethods.xmax_client_get_capabilities(lease.Value, &value));
        if (value.abi_version != 2) throw new NotSupportedException($"Unsupported native ABI {value.abi_version}.");
        return (XmaxCapabilities)value.flags;
    }

    /// <summary>
    /// Queries model formats, defaults, and input limits without creating a client or network session.
    /// </summary>
    /// <param name="model">The supported realtime model. The default is X2.1.</param>
    /// <returns>The model capability snapshot.</returns>
    public static unsafe ModelInfo GetModelInfo(
        RealtimeModel model = RealtimeModel.X2_1
    )
    {
        NativeLibraryLoader.Initialize();
        using var name = new Utf8(GetModelIdentifier(model));
        var info = new xmax_model_info { struct_size = (uint)sizeof(xmax_model_info) };
        NativeError.Check(NativeMethods.xmax_model_get_info(name.Pointer, &info));
        xmax_video_format* formats = &info.supported_formats_0;
        var values = new VideoFormat[checked((int)info.format_count)];
        if (values.Length > 5) throw new NotSupportedException("Invalid model format count.");
        for (int i = 0; i < values.Length; ++i) values[i] = VideoFormat.From(formats[i]);
        return new(NativeError.Text(info.identifier), NativeError.Text(info.display_name), values,
            VideoFormat.From(info.default_camera_format), info.minimum_input_pixels, info.maximum_input_pixels, info.input_size_alignment);
    }

    /// <summary>Creates an independent realtime controller and captures the current UI context.</summary>
    /// <param name="configuration">The model and adaptive-quality settings; null selects the defaults.</param>
    /// <returns>A controller that your app must release with DisposeAsync.</returns>
    public IXmaxRealtimeManaging CreateRealtimeManager(RealtimeConfiguration? configuration = null) =>
        CreateRealtimeManager(configuration, SynchronizationContext.Current);

    /// <summary>Creates an independent realtime controller with a specified UI context.</summary>
    /// <param name="configuration">The model and adaptive-quality settings; null selects the defaults.</param>
    /// <param name="callbackContext">The UI context for typed observations and external source lifecycle.
    /// When null, each operation uses its caller's synchronization context if available.</param>
    /// <returns>A controller that your app must release with DisposeAsync.</returns>
    public unsafe IXmaxRealtimeManaging CreateRealtimeManager(
        RealtimeConfiguration? configuration,
        SynchronizationContext? callbackContext
    )
    {
        configuration ??= new();
        using var lease = handle.Borrow();
        using var model = new Utf8(GetModelIdentifier(configuration.Model));
        var options = new xmax_realtime_options { struct_size = (uint)sizeof(xmax_realtime_options),
            model = model.Pointer, adaptive_quality_enabled = configuration.AdaptiveQualityEnabled ? 1 : 0 };
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_realtime_create_with_options(lease.Value, &options, &value));
        var realtime = new NativeHandle(value, HandleKind.Realtime);
        try
        {
            return new XmaxRealtime(realtime, configuration, callbackContext);
        }
        catch
        {
            realtime.Dispose();
            throw;
        }
    }

    internal static string GetModelIdentifier(RealtimeModel model) => model switch
    {
        RealtimeModel.X2_1 => "x2.1",
        RealtimeModel.X2_1Flash => "x2.1-flash",
        _ => throw new ArgumentOutOfRangeException(nameof(model))
    };

    /// <summary>Creates a media sizing and capability service for the selected model.</summary>
    public IMediaServicing CreateMediaService(RealtimeModel model = RealtimeModel.X2_1)
    {
        NativeLibraryLoader.Initialize();
        _ = GetModelIdentifier(model);
        return new XmaxMediaService(model);
    }

    /// <summary>
    /// Creates an independent storage controller.
    /// </summary>
    /// <returns>A storage controller that your app must release with DisposeAsync.</returns>
    /// <exception cref="XmaxException">The configured API key is empty.</exception>
    public unsafe IXmaxStorageManaging CreateStorageManager()
    {
        using var lease = handle.Borrow();
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_storage_create(lease.Value, &value));
        return new XmaxStorage(new(value, HandleKind.Storage));
    }

    /// <summary>
    /// Releases this client handle without closing previously created child controllers.
    /// </summary>
    /// <remarks>
    /// Wait for your app to finish concurrent uses of this client before disposal.
    /// </remarks>
    public void Dispose() => handle.Dispose();
}
