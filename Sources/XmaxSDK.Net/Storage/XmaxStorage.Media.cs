//
//  XmaxStorage.Media.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xmax.SDK.Interop;

namespace Xmax.SDK;

internal sealed partial class XmaxStorage
{
    private const int Image = 1;
    private const int Video = 2;

    /// <summary>
    /// Uploads a local image without performing a safety check.
    /// </summary>
    /// <param name="path">The path to the encoded file.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the path.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    public Task<UploadedFile> UploadImageAsync(
        string path,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => UploadMediaAsync(
        Image,
        path,
        default,
        new(ContentType: contentType, CheckImageSafety: false),
        progress,
        cancellationToken
    );

    /// <summary>
    /// Uploads an encoded image without performing a safety check.
    /// </summary>
    /// <param name="data">The encoded bytes. Keep the memory unchanged until the task completes.</param>
    /// <param name="fileName">The object filename and MIME type hint.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the filename.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    public Task<UploadedFile> UploadImageAsync(
        ReadOnlyMemory<byte> data,
        string fileName,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => UploadMediaAsync(
        Image,
        null,
        data,
        new(fileName, contentType, false),
        progress,
        cancellationToken
    );

    /// <summary>
    /// Uploads a local image with a service safety check.
    /// </summary>
    /// <param name="path">The path to the encoded file.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the path.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    public Task<UploadedFile> UploadImageWithSafetyCheckAsync(
        string path,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => UploadMediaAsync(
        Image,
        path,
        default,
        new(ContentType: contentType, CheckImageSafety: true),
        progress,
        cancellationToken
    );

    /// <summary>
    /// Uploads an encoded image with a service safety check.
    /// </summary>
    /// <param name="data">The encoded bytes. Keep the memory unchanged until the task completes.</param>
    /// <param name="fileName">The object filename and MIME type hint.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the filename.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    public Task<UploadedFile> UploadImageWithSafetyCheckAsync(
        ReadOnlyMemory<byte> data,
        string fileName,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => UploadMediaAsync(
        Image,
        null,
        data,
        new(fileName, contentType, true),
        progress,
        cancellationToken
    );

    /// <summary>
    /// Uploads a local video.
    /// </summary>
    /// <param name="path">The path to the encoded file.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the path.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    public Task<UploadedFile> UploadVideoAsync(
        string path,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => UploadMediaAsync(
        Video,
        path,
        default,
        new(ContentType: contentType, CheckImageSafety: false),
        progress,
        cancellationToken
    );

    /// <summary>
    /// Uploads an encoded video.
    /// </summary>
    /// <param name="data">The encoded bytes. Keep the memory unchanged until the task completes.</param>
    /// <param name="fileName">The object filename and MIME type hint.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the filename.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    public Task<UploadedFile> UploadVideoAsync(
        ReadOnlyMemory<byte> data,
        string fileName,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => UploadMediaAsync(
        Video,
        null,
        data,
        new(fileName, contentType, false),
        progress,
        cancellationToken
    );

    /// <summary>
    /// Downloads a remote image and replaces the destination only after the transfer succeeds.
    /// </summary>
    /// <param name="url">The HTTP or HTTPS source URL.</param>
    /// <param name="destination">The local file path. The destination must not be a directory.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels the transfer and removes the incomplete file.</param>
    /// <returns>The destination path and completed byte count.</returns>
    public Task<DownloadedFile> DownloadImageAsync(
        string url,
        string destination,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => DownloadMediaAsync(
        url,
        destination,
        progress,
        cancellationToken
    );

    /// <summary>
    /// Downloads a remote video and replaces the destination only after the transfer succeeds.
    /// </summary>
    /// <param name="url">The HTTP or HTTPS source URL.</param>
    /// <param name="destination">The local file path. The destination must not be a directory.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels the transfer and removes the incomplete file.</param>
    /// <returns>The destination path and completed byte count.</returns>
    public Task<DownloadedFile> DownloadVideoAsync(
        string url,
        string destination,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    ) => DownloadMediaAsync(
        url,
        destination,
        progress,
        cancellationToken
    );

    private Task<UploadedFile> UploadMediaAsync(int kind, string? path, ReadOnlyMemory<byte> data,
        UploadOptions options, IProgress<TransferProgress>? progress, CancellationToken token) =>
        operations.Run(value => UploadMedia(value, kind, path, data, options, new(progress, token)), token);

    private Task<DownloadedFile> DownloadMediaAsync(string url, string destination,
        IProgress<TransferProgress>? progress, CancellationToken token) =>
        operations.Run(value => DownloadMedia(value, url, destination, new(progress, token)), token);

    private static unsafe UploadedFile UploadMedia(nint value, int kind, string? path, ReadOnlyMemory<byte> data,
        UploadOptions options, TransferCallbacks callbacks)
    {
        using var file = new Utf8(path);
        using var name = new Utf8(options.FileName);
        using var type = new Utf8(options.ContentType);
        using var bytes = data.Pin();
        var native = new xmax_upload_options
        {
            struct_size = (uint)sizeof(xmax_upload_options), file_name = name.Pointer,
            content_type = type.Pointer, check_image_safety = options.CheckImageSafety ? 1 : 0
        };
        var context = GCHandle.Alloc(callbacks);
        try
        {
            nint result = 0;
            int status = NativeMethods.xmax_storage_upload_media(value, file.Pointer, (byte*)bytes.Pointer,
                (nuint)data.Length, &native, kind, &result, &ReportProgress, &IsCancelled, (void*)GCHandle.ToIntPtr(context));
            if (status < 0) callbacks.Token.ThrowIfCancellationRequested();
            NativeError.Check(status);
            using var owner = new NativeHandle(result, HandleKind.UploadedFile);
            var info = new xmax_uploaded_file_info { struct_size = (uint)sizeof(xmax_uploaded_file_info) };
            NativeError.Check(NativeMethods.xmax_uploaded_file_get_info(result, &info));
            return new(NativeError.Text(info.url), NativeError.Text(info.object_key), NativeError.Text(info.etag));
        }
        finally { context.Free(); }
    }

    private static unsafe DownloadedFile DownloadMedia(nint value, string url, string destination, TransferCallbacks callbacks)
    {
        using var source = new Utf8(url);
        using var path = new Utf8(destination);
        var context = GCHandle.Alloc(callbacks);
        try
        {
            ulong count;
            int status = NativeMethods.xmax_storage_download_with_progress(value, source.Pointer, path.Pointer,
                &count, &ReportProgress, &IsCancelled, (void*)GCHandle.ToIntPtr(context));
            if (status < 0) callbacks.Token.ThrowIfCancellationRequested();
            NativeError.Check(status);
            return new(destination, count);
        }
        finally { context.Free(); }
    }

    private sealed record TransferCallbacks(IProgress<TransferProgress>? Progress, CancellationToken Token);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ReportProgress(nint context, ulong completed, ulong total)
    {
        var callbacks = (TransferCallbacks)GCHandle.FromIntPtr(context).Target!;
        try { callbacks.Progress?.Report(new(1, completed, total)); }
        catch (Exception) { /* A progress observer cannot unwind into native transfer work. */ }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int IsCancelled(nint context) =>
        ((TransferCallbacks)GCHandle.FromIntPtr(context).Target!).Token.IsCancellationRequested ? 1 : 0;
}
