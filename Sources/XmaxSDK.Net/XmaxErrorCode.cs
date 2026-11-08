//
//  XmaxErrorCode.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Stable categories for failures reported by the SDK.</summary>
public enum XmaxErrorCode
{
    /// <summary>Invalid arguments or configuration.</summary>
    InvalidConfiguration = -1,
    /// <summary>The current lifecycle state cannot perform the operation.</summary>
    InvalidState = -2,
    /// <summary>Network transport failed.</summary>
    NetworkError = -3,
    /// <summary>The service rejected the request or returned an invalid response.</summary>
    ApiError = -4,
    /// <summary>RTC setup, signaling, or media transport failed.</summary>
    RtcError = -5,
    /// <summary>The operation exceeded its deadline.</summary>
    Timeout = -6,
    /// <summary>The operation was cancelled.</summary>
    Cancelled = -7,
    /// <summary>The platform or SDK build lacks the requested capability.</summary>
    Unsupported = -8,
    /// <summary>An unclassified SDK error.</summary>
    InternalError = -9,
    /// <summary>The destination buffer is too small.</summary>
    BufferTooSmall = -10,
    /// <summary>Local capture, decoding, or media processing failed.</summary>
    MediaError = -11,
    /// <summary>Local file access or destination publication failed.</summary>
    IoError = -12,
    /// <summary>The image failed the requested safety check.</summary>
    UnsafeImage = -13,
    /// <summary>The API key is absent or invalid.</summary>
    InvalidApiKey = -14,
    /// <summary>The session or its heartbeat is invalid.</summary>
    SessionError = -15,
    /// <summary>The system denied access to the camera.</summary>
    CameraPermissionDenied = -16,
    /// <summary>The system denied access to the microphone.</summary>
    MicrophonePermissionDenied = -17,
    /// <summary>The upload failed.</summary>
    UploadError = -18,
    /// <summary>The download failed.</summary>
    DownloadError = -19
}
