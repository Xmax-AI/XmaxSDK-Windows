//
//  LogRecord.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// A copied process-wide SDK diagnostic record.
/// </summary>
/// <param name="Level">The severity: 0 debug, 1 info, 2 warning, or 3 error.</param>
/// <param name="Category">The diagnostic category, such as RTC, Media, API, or Storage.</param>
/// <param name="Message">The bounded, credential-redacted diagnostic text.</param>
/// <param name="TimestampMicroseconds">The record time, in microseconds since the Unix epoch.</param>
/// <param name="Sequence">The process-wide sequence number; gaps can indicate discarded queued records.</param>
public sealed record LogRecord(
    int Level,
    string Category,
    string Message,
    long TimestampMicroseconds,
    ulong Sequence
);
