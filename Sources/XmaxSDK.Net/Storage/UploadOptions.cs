//
//  UploadOptions.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// File metadata and image-safety policy for an upload.
/// </summary>
/// <param name="FileName">The file name used for object naming and MIME inference; supply a name for memory uploads.</param>
/// <param name="ContentType">The MIME type; null lets the SDK infer it from the file name.</param>
/// <param name="CheckImageSafety">Whether to request a safety check for an image upload.</param>
public sealed record UploadOptions(
    string? FileName = null,
    string? ContentType = null,
    bool CheckImageSafety = true
);
