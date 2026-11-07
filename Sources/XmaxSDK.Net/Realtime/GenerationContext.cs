//
//  GenerationContext.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>The text and optional uploaded reference path for a realtime generation request.</summary>
public sealed record GenerationContext
{
    /// <summary>Gets the generation instruction with surrounding whitespace removed.</summary>
    public string Prompt { get; }

    /// <summary>Gets the reference path or URL, or null when omitted.</summary>
    public string? ReferencePath { get; }

    /// <summary>Creates the generation context and normalizes its text and reference path.</summary>
    public GenerationContext(string prompt, string? referencePath = null)
    {
        ArgumentNullException.ThrowIfNull(prompt);
        Prompt = Normalize(prompt);
        if (referencePath is not null)
        {
            var normalized = Normalize(referencePath);
            ReferencePath = normalized.Length == 0 ? null : normalized;
        }
    }

    private static string Normalize(string value)
    {
        int start = 0, end = value.Length;
        while (start < end && IsWhitespace(value[start])) ++start;
        while (end > start && IsWhitespace(value[end - 1])) --end;
        return value[start..end];
    }

    // Foundation includes zero-width space in whitespacesAndNewlines.
    private static bool IsWhitespace(char value) => char.IsWhiteSpace(value) || value == '\u200B';
}
