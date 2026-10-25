//
//  XmaxConfiguration.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// Global settings copied when a client is created.
/// </summary>
public sealed record XmaxConfiguration
{

    /// <summary>
    /// Gets the service API credential; an empty value permits local preparation but not cloud connection.
    /// </summary>
    public string ApiKey { get; init; } = "";

    /// <summary>
    /// Gets the service environment; the default is China.
    /// </summary>
    public XmaxEnvironment Environment { get; init; } = XmaxEnvironment.China;

    /// <summary>
    /// Gets the HTTP timeout, in milliseconds; the default and a value of 0 select 15000 ms.
    /// </summary>
    public uint HttpTimeoutMilliseconds { get; init; } = 15000;

    /// <summary>
    /// Gets the process-wide LogOptions bitmask applied at client creation; 0 disables logging.
    /// </summary>
    /// <remarks>
    /// Client creation applies this mask and the environment language, clearing queued records. Use XmaxLogger for subsequent explicit changes.
    /// </remarks>
    public uint LoggerOptions { get; init; }
    // Avoid accidentally formatting credentials in record-generated diagnostics.

    /// <summary>
    /// Formats the service environment without including the API credential.
    /// </summary>
    /// <returns>A diagnostic representation with no API Key.</returns>
    public override string ToString() => $"XmaxConfiguration {{ Environment = {Environment} }}";
}
