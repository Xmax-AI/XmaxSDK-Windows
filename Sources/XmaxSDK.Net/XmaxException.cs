//
//  XmaxException.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>
/// An SDK failure containing native, service, and HTTP diagnostic codes.
/// </summary>
/// <param name="result">The negative SDK result code.</param>
/// <param name="message">The diagnostic message copied from the failing native call.</param>
/// <param name="apiCode">The service business error code, or 0 when absent.</param>
/// <param name="httpStatus">The HTTP status code, or 0 when absent.</param>
public sealed class XmaxException(
    int result,
    string message,
    int apiCode = 0,
    int httpStatus = 0
) : Exception(message)
{

    /// <summary>
    /// Gets the native SDK result code.
    /// </summary>
    public int Result { get; } = result;

    /// <summary>
    /// Gets the service business error code, or 0 when absent.
    /// </summary>
    public int ApiCode { get; } = apiCode;

    /// <summary>
    /// Gets the HTTP status code, or 0 when absent.
    /// </summary>
    public int HttpStatus { get; } = httpStatus;
}
