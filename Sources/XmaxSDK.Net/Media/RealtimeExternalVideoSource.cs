//
//  RealtimeExternalVideoSource.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Supplies app-owned video and optional PCM to a realtime stream.</summary>
/// <remarks>
/// Lifecycle methods and source properties run on the controller's UI context when available.
/// Frames can be submitted from a capture worker. Do not reenter controller lifecycle operations
/// from these callbacks.
/// </remarks>
public interface IRealtimeExternalVideoSource
{
    /// <summary>Gets the dimensions and frame rate of the submitted video.</summary>
    VideoFormat VideoFormat { get; }

    /// <summary>Gets whether the source supplies PCM on the video clock.</summary>
    bool HasAudio => false;

    /// <summary>Installs frame delivery and returns when the source is ready to submit frames.</summary>
    /// <param name="sink">The receiver owned by the SDK for this attachment.</param>
    /// <param name="cancellationToken">Cancels source startup.</param>
    /// <returns>A task that completes once the capture callback is installed.</returns>
    Task StartAsync(
        IRealtimeExternalFrameSink sink,
        CancellationToken cancellationToken
    );

    /// <summary>Stops submissions, drains capture callbacks and releases the reference to the sink.</summary>
    /// <returns>A task that completes once frame delivery has stopped.</returns>
    Task StopAsync();
}
