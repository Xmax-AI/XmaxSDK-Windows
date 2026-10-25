using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;


public sealed class XmaxStorage : IAsyncDisposable
{
    private readonly NativeHandle handle;
    private readonly OperationTracker operations;
    internal XmaxStorage(NativeHandle handle) { this.handle = handle; operations = new(handle); }
    public Task<UploadedFile> UploadFileAsync(string path, UploadOptions? options = null, CancellationToken cancellationToken = default) =>
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
    public Task<ulong> DownloadAsync(string url, string destination, CancellationToken cancellationToken = default) =>
        operations.Run(value => NativeCancellation.Run(cancellationToken, () => Download(value, url, destination),
            () => NativeMethods.xmax_storage_cancel(value)), cancellationToken);
    /// <summary>The supplied memory must not be mutated until the returned task completes.</summary>
    public Task<UploadedFile> UploadDataAsync(ReadOnlyMemory<byte> data, UploadOptions options, CancellationToken cancellationToken = default) =>
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
    public unsafe TransferProgress GetProgress()
    {
        using var lease = handle.Borrow();
        var value = new xmax_transfer_progress { struct_size = (uint)sizeof(xmax_transfer_progress) };
        NativeError.Check(NativeMethods.xmax_storage_get_progress(lease.Value, &value));
        return new(value.state, value.transferred, value.total);
    }
    public void Cancel() { using var lease = handle.Borrow(); NativeMethods.xmax_storage_cancel(lease.Value); }
    public ValueTask DisposeAsync() => new(operations.DisposeAsync(NativeMethods.xmax_storage_cancel));
}
