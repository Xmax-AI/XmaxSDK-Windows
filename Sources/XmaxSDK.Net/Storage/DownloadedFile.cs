//
//  DownloadedFile.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>A completed download.</summary>
/// <param name="Path">The destination file path.</param>
/// <param name="Bytes">The number of bytes written.</param>
public sealed record DownloadedFile(string Path, ulong Bytes);
