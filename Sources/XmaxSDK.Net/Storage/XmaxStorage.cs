//
//  XmaxStorage.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;

/// <summary>
/// An owner of cloud upload and download operations with progress and cancellation.
/// </summary>
/// <remarks>
/// Each controller permits one active transfer. Use independent storage controllers for concurrent transfers.
/// Await operations instead of blocking the UI thread; native failures are reported as XmaxException.
/// </remarks>
public sealed class XmaxStorage : IAsyncDisposable
{
    private readonly NativeHandle handle;

    private readonly OperationTracker operations;

    internal XmaxStorage(NativeHandle handle) { this.handle = handle; operations = new(handle); }

    /// <summary>
    /// Uploads an encoded image or video file, optionally checking image safety.
    /// </summary>
    /// <param name="path">The readable local file path.</param>
    /// <param name="options">The upload metadata and safety policy; null selects defaults and derives a file name from the path.</param>
    /// <param name="cancellationToken">Cancels this controller's active transfer and waits for cancellation cleanup.</param>
    /// <returns>A task containing copied metadata for the uploaded object.</returns>
    /// <remarks>
    /// Image and video size limits are 20 MiB and 256 MiB, respectively. Files are read into memory; resumable multipart upload is not provided.
    /// </remarks>
    public Task<UploadedFile> UploadFileAsync(
        string path,
        UploadOptions? options = null,
        CancellationToken cancellationToken = default
    ) =>
        operations.Run(value => NativeCancellation.Run(cancellationToken, () => Upload(value, path, options),
            () => NativeMethods.xmax_storage_cancel(value)), cancellationToken);

    private static unsafe UploadedFile Upload(nint value, string path, UploadOptions? options)
    {
        options ??= new();
        using var filename = new Utf8(options.FileName);
        using var type = new Utf8(options.ContentType);
        using var file = new Utf8(path);
        var native = new xmax_upload_options { struct_size = (uint)sizeof(xmax_upload_options), file_name = filename.Pointer,
            content_type = type.Pointer, check_image_safety = options.CheckImageSafety ? 1 : 0 };
        nint result = 0;
        NativeError.Check(NativeMethods.xmax_storage_upload_file(value, file.Pointer, &native, &result));
        using var owner = new NativeHandle(result, HandleKind.UploadedFile);
        var info = new xmax_uploaded_file_info { struct_size = (uint)sizeof(xmax_uploaded_file_info) };
        NativeError.Check(NativeMethods.xmax_uploaded_file_get_info(result, &info));
        return new(NativeError.Text(info.url), NativeError.Text(info.object_key), NativeError.Text(info.etag));
    }

    /// <summary>
    /// Downloads an HTTPS object to a new local file.
    /// </summary>
    /// <param name="url">The HTTPS source URL; redirects are not followed.</param>
    /// <param name="destination">The destination path, which must not already exist.</param>
    /// <param name="cancellationToken">Cancels this controller's active transfer and removes its incomplete temporary file.</param>
    /// <returns>A task containing the downloaded byte count.</returns>
    /// <remarks>
    /// Downloads are limited to 256 MiB. Data is written to a temporary file and published to the destination only on success.
    /// </remarks>
    public Task<ulong> DownloadAsync(
        string url,
        string destination,
        CancellationToken cancellationToken = default
    ) =>
        operations.Run(value => NativeCancellation.Run(cancellationToken, () => Download(value, url, destination),
            () => NativeMethods.xmax_storage_cancel(value)), cancellationToken);

    /// <summary>
    /// Uploads encoded file bytes without taking ownership of the supplied memory.
    /// </summary>
    /// <param name="data">The file bytes; keep the memory unchanged until the returned task completes.</param>
    /// <param name="options">The file name, MIME type, and image-safety policy.</param>
    /// <param name="cancellationToken">Cancels this controller's active transfer and waits for cancellation cleanup.</param>
    /// <returns>A task containing copied metadata for the uploaded object.</returns>
    /// <remarks>
    /// The input is an encoded file, not raw frame pixels. File size limits are the same as UploadFileAsync.
    /// </remarks>
    public Task<UploadedFile> UploadDataAsync(
        ReadOnlyMemory<byte> data,
        UploadOptions options,
        CancellationToken cancellationToken = default
    ) =>
        operations.Run(value => NativeCancellation.Run(cancellationToken, () => UploadData(value, data, options),
            () => NativeMethods.xmax_storage_cancel(value)), cancellationToken);

    private static unsafe UploadedFile UploadData(nint value, ReadOnlyMemory<byte> data, UploadOptions options)
    {
        using var bytes = data.Pin(); using var filename = new Utf8(options.FileName); using var type = new Utf8(options.ContentType);
        var native = new xmax_upload_options { struct_size = (uint)sizeof(xmax_upload_options), file_name = filename.Pointer,
            content_type = type.Pointer, check_image_safety = options.CheckImageSafety ? 1 : 0 };
        nint result = 0;
        NativeError.Check(NativeMethods.xmax_storage_upload_data(value, (byte*)bytes.Pointer, (nuint)data.Length, &native, &result));
        using var owner = new NativeHandle(result, HandleKind.UploadedFile);
        var info = new xmax_uploaded_file_info { struct_size = (uint)sizeof(xmax_uploaded_file_info) };
        NativeError.Check(NativeMethods.xmax_uploaded_file_get_info(result, &info));
        return new(NativeError.Text(info.url), NativeError.Text(info.object_key), NativeError.Text(info.etag));
    }

    private static unsafe ulong Download(nint value, string url, string destination)
    {
        using var source = new Utf8(url);
        using var path = new Utf8(destination);
        ulong count = 0;
        NativeError.Check(NativeMethods.xmax_storage_download(value, source.Pointer, path.Pointer, &count));
        return count;
    }

    /// <summary>
    /// Reads transfer progress without waiting for the transfer to finish.
    /// </summary>
    /// <returns>The current or most recent transfer state and byte counts.</returns>
    public unsafe TransferProgress GetProgress()
    {
        using var lease = handle.Borrow();
        var value = new xmax_transfer_progress { struct_size = (uint)sizeof(xmax_transfer_progress) };
        NativeError.Check(NativeMethods.xmax_storage_get_progress(lease.Value, &value));
        return new(value.state, value.transferred, value.total);
    }

    /// <summary>
    /// Requests cancellation of the current transfer without waiting for cleanup.
    /// </summary>
    /// <remarks>
    /// Cancellation takes effect at native network checkpoints and is bounded by the current network-stage timeout.
    /// The controller can be reused after the active operation finishes.
    /// </remarks>
    public void Cancel()
    {
        using var lease = handle.Borrow();
        NativeMethods.xmax_storage_cancel(lease.Value);
    }

    /// <summary>
    /// Cancels and drains in-flight storage work before releasing the native controller.
    /// </summary>
    /// <returns>An awaitable operation completing after cleanup.</returns>
    /// <remarks>
    /// New operations are rejected once disposal starts.
    /// </remarks>
    public ValueTask DisposeAsync() => new(operations.DisposeAsync(NativeMethods.xmax_storage_cancel));
}
