//
//  UploadedFile.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// Copied metadata for a completed upload, independent of the native upload-result handle.
/// </summary>
/// <param name="Url">The uploaded object URL, usable as a generation reference when supported by the service.</param>
/// <param name="ObjectKey">The object key assigned by storage.</param>
/// <param name="ETag">The ETag returned by storage.</param>
public sealed record UploadedFile(
    string Url,
    string ObjectKey,
    string ETag
);
