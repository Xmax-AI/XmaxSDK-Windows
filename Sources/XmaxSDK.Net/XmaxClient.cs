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
/// An owner of client configuration used to create realtime controllers and storage objects.
/// </summary>
/// <remarks>
/// Creating a client starts no capture or network session. Child controllers retain their own configuration
/// and remain usable after this client is disposed. Native failures are reported as XmaxException.
/// </remarks>
public sealed class XmaxClient : IDisposable
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
            struct_size = (uint)sizeof(xmax_client_options), abi_version = 1, api_key = key.Pointer,
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
        if (value.abi_version != 1) throw new NotSupportedException($"Unsupported native ABI {value.abi_version}.");
        return (XmaxCapabilities)value.flags;
    }

    /// <summary>
    /// Queries model formats, defaults, and input limits without creating a client or network session.
    /// </summary>
    /// <param name="model">The supported service model identifier; currently x2.1.</param>
    /// <returns>The model capability snapshot.</returns>
    public static unsafe ModelInfo GetModelInfo(
        string model = "x2.1"
    )
    {
        NativeLibraryLoader.Initialize();
        using var name = new Utf8(model);
        var info = new xmax_model_info { struct_size = (uint)sizeof(xmax_model_info) };
        NativeError.Check(NativeMethods.xmax_model_get_info(name.Pointer, &info));
        xmax_video_format* formats = &info.supported_formats_0;
        var values = new VideoFormat[checked((int)info.format_count)];
        if (values.Length > 5) throw new NotSupportedException("Invalid model format count.");
        for (int i = 0; i < values.Length; ++i) values[i] = VideoFormat.From(formats[i]);
        return new(NativeError.Text(info.identifier), NativeError.Text(info.display_name), values,
            VideoFormat.From(info.default_camera_format), info.minimum_input_pixels, info.maximum_input_pixels, info.input_size_alignment);
    }

    /// <summary>
    /// Enumerates camera devices without starting capture.
    /// </summary>
    /// <param name="cancellationToken">Cancels before device enumeration starts; it cannot interrupt native enumeration.</param>
    /// <returns>A task containing a copied, read-only device snapshot.</returns>
    public Task<IReadOnlyList<CameraDevice>> ListCamerasAsync(
        CancellationToken cancellationToken = default
    )
    {
        var lease = handle.Borrow();
        return Task.Run<IReadOnlyList<CameraDevice>>(() =>
        {
            using (lease)
            {
                cancellationToken.ThrowIfCancellationRequested();
                return EnumerateCameras(lease.Value);
            }
        });
    }

    private static unsafe IReadOnlyList<CameraDevice> EnumerateCameras(nint client)
    {
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_client_list_cameras(client, &value));
        using var list = new NativeHandle(value, HandleKind.CameraList);
        nuint count = NativeMethods.xmax_camera_list_count(value);
        var result = new List<CameraDevice>();
        for (nuint i = 0; i < count; ++i)
        {
            var info = new xmax_camera_info { struct_size = (uint)sizeof(xmax_camera_info) };
            NativeError.Check(NativeMethods.xmax_camera_list_get(value, i, &info));
            result.Add(new(NativeError.Text(info.id), NativeError.Text(info.name), info.is_default != 0));
        }
        return result.AsReadOnly();
    }

    /// <summary>
    /// Creates an independent realtime controller from this client.
    /// </summary>
    /// <param name="configuration">The model and adaptive-quality settings; null selects the defaults.</param>
    /// <returns>A controller that your app must release with DisposeAsync.</returns>
    public unsafe XmaxRealtime CreateRealtime(
        RealtimeConfiguration? configuration = null
    )
    {
        configuration ??= new();
        using var lease = handle.Borrow();
        using var model = new Utf8(configuration.Model);
        var options = new xmax_realtime_options { struct_size = (uint)sizeof(xmax_realtime_options),
            model = model.Pointer, adaptive_quality_enabled = configuration.AdaptiveQualityEnabled ? 1 : 0 };
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_realtime_create_with_options(lease.Value, &options, &value));
        return new(new(value, HandleKind.Realtime));
    }

    /// <summary>
    /// Creates an independent storage controller.
    /// </summary>
    /// <returns>A storage controller that your app must release with DisposeAsync.</returns>
    public unsafe XmaxStorage CreateStorage()
    {
        using var lease = handle.Borrow();
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_storage_create(lease.Value, &value));
        return new(new(value, HandleKind.Storage));
    }

    /// <summary>
    /// Sends a JSON request to a relative path on the configured general API service.
    /// </summary>
    /// <param name="method">The HTTP method supported by the service.</param>
    /// <param name="path">The relative API path; absolute URLs are rejected.</param>
    /// <param name="jsonBody">The JSON request body; null omits the body.</param>
    /// <param name="cancellationToken">Cancels before the native request starts; an admitted request has no in-flight cancellation.</param>
    /// <returns>A task containing the response JSON text.</returns>
    public Task<string> RequestJsonAsync(
        string method,
        string path,
        string? jsonBody = null,
        CancellationToken cancellationToken = default
    )
    {
        var lease = handle.Borrow();
        return Task.Run(() =>
        {
            using (lease) { cancellationToken.ThrowIfCancellationRequested(); return RequestJson(lease.Value, method, path, jsonBody); }
        });
    }

    private static unsafe string RequestJson(nint value, string method, string path, string? body)
    {
        using var verb = new Utf8(method); using var target = new Utf8(path); using var json = new Utf8(body);
        nint result = 0;
        NativeError.Check(NativeMethods.xmax_client_request_json(value, verb.Pointer, target.Pointer, json.Pointer, &result));
        using var owner = new NativeHandle(result, HandleKind.String);
        return NativeError.Text(NativeMethods.xmax_string_data(result));
    }

    /// <summary>
    /// Releases this client handle without closing previously created child controllers.
    /// </summary>
    /// <remarks>
    /// Wait for your app to finish concurrent uses of this client before disposal.
    /// </remarks>
    public void Dispose() => handle.Dispose();
}
