//
//  IXmaxStorageManaging.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Transfers typed media with per-operation progress and cancellation.</summary>
public interface IXmaxStorageManaging : IAsyncDisposable
{
    /// <summary>
    /// Uploads a local image without performing a safety check.
    /// </summary>
    /// <param name="path">The path to the encoded file.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the path.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    Task<UploadedFile> UploadImageAsync(
        string path,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
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
    Task<UploadedFile> UploadImageAsync(
        ReadOnlyMemory<byte> data,
        string fileName,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    );

    /// <summary>
    /// Uploads a local image with a service safety check.
    /// </summary>
    /// <param name="path">The path to the encoded file.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the path.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    Task<UploadedFile> UploadImageWithSafetyCheckAsync(
        string path,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
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
    Task<UploadedFile> UploadImageWithSafetyCheckAsync(
        ReadOnlyMemory<byte> data,
        string fileName,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    );

    /// <summary>
    /// Uploads a local video.
    /// </summary>
    /// <param name="path">The path to the encoded file.</param>
    /// <param name="contentType">The optional MIME type. Null infers it from the path.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels this transfer.</param>
    /// <returns>The uploaded object's URL and metadata.</returns>
    Task<UploadedFile> UploadVideoAsync(
        string path,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
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
    Task<UploadedFile> UploadVideoAsync(
        ReadOnlyMemory<byte> data,
        string fileName,
        string? contentType = null,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    );

    /// <summary>
    /// Downloads a remote image and replaces the destination only after the transfer succeeds.
    /// </summary>
    /// <param name="url">The HTTP or HTTPS source URL.</param>
    /// <param name="destination">The local file path. The destination must not be a directory.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels the transfer and removes the incomplete file.</param>
    /// <returns>The destination path and completed byte count.</returns>
    Task<DownloadedFile> DownloadImageAsync(
        string url,
        string destination,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    );

    /// <summary>
    /// Downloads a remote video and replaces the destination only after the transfer succeeds.
    /// </summary>
    /// <param name="url">The HTTP or HTTPS source URL.</param>
    /// <param name="destination">The local file path. The destination must not be a directory.</param>
    /// <param name="progress">Receives byte progress for this transfer.</param>
    /// <param name="cancellationToken">Cancels the transfer and removes the incomplete file.</param>
    /// <returns>The destination path and completed byte count.</returns>
    Task<DownloadedFile> DownloadVideoAsync(
        string url,
        string destination,
        IProgress<TransferProgress>? progress = null,
        CancellationToken cancellationToken = default
    );
}
