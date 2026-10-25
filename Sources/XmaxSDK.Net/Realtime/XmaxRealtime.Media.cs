//
//  XmaxRealtime.Media.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;

public sealed partial class XmaxRealtime
{

    /// <summary>
    /// Registers one serialized listener for result-user PCM and decoded video on a shared rendering timeline.
    /// </summary>
    /// <param name="listener">The background callback owning each received packet; dispose or transfer every packet. Null clears the listener and queued output.</param>
    /// <returns>A task completing after listener registration changes.</returns>
    /// <remarks>
    /// Registration may wait for lifecycle work and does not change playback volume or subscriptions.
    /// An in-flight callback may finish after replacement or removal; captured context stays alive through that callback.
    /// Forward packets promptly to avoid bounded-queue drops. Synchronous callback exceptions are reported through CallbackFaulted.
    /// </remarks>
    public Task SetRemoteMediaListenerAsync(
        Action<XmaxMediaFrame>? listener
    ) =>
        operations.Run(value => { RemoteMediaRegistration.Set(value, listener, NotifyCallbackFault); return 0; });

    /// <summary>
    /// Binds caller-owned video and optional PCM input while disconnected.
    /// </summary>
    /// <param name="format">The fixed external video dimensions and nominal frame rate.</param>
    /// <param name="hasAudio">Whether this source supplies PCM audio instead of RTC microphone capture.</param>
    /// <returns>A source-bound receiver that your app must dispose after capture submissions stop.</returns>
    /// <remarks>
    /// Your app owns capture, permission, and pacing. Frames can be submitted for local preview while disconnected.
    /// Replacing or closing the binding makes old receivers ignore subsequent submissions.
    /// </remarks>
    public Task<ExternalFrameSink> BindExternalAsync(
        VideoFormat format,
        bool hasAudio = false
    ) => operations.Run(value => BindExternal(value, format, hasAudio));

    private static unsafe ExternalFrameSink BindExternal(nint value, VideoFormat format, bool hasAudio)
    {
        var native = new xmax_external_media_config { struct_size = (uint)sizeof(xmax_external_media_config), video_format = format.Native, has_audio = hasAudio ? 1 : 0 };
        nint sink = 0;
        NativeError.Check(NativeMethods.xmax_realtime_bind_external_media(value, &native, &sink));
        return new(new(sink, HandleKind.ExternalSink), format);
    }

    /// <summary>
    /// Maps viewport trajectory samples to video coordinates and queues tracks for the current generation task.
    /// </summary>
    /// <param name="points">From 1 through 256 points in the same coordinate units as the viewport.</param>
    /// <param name="viewportWidth">The display viewport width; must be positive and finite.</param>
    /// <param name="viewportHeight">The display viewport height; must be positive and finite.</param>
    /// <param name="videoWidth">The actual displayed video width, in pixels.</param>
    /// <param name="videoHeight">The actual displayed video height, in pixels.</param>
    /// <param name="mode">The aspect-ratio layout used to display this video.</param>
    /// <remarks>
    /// Generation must be ready for interaction. Aspect-fit margins are excluded. This call accepts input without waiting for network transmission; pending samples are coalesced.
    /// Asynchronous send failures are reported through realtime events.
    /// </remarks>
    public unsafe void SubmitInteraction(
        ReadOnlySpan<TrajectoryPoint> points,
        double viewportWidth,
        double viewportHeight,
        uint videoWidth,
        uint videoHeight,
        VideoContentMode mode = VideoContentMode.Fit
    )
    {
        using var lease = handle.Borrow();
        if (points.Length is < 1 or > 256) throw new ArgumentOutOfRangeException(nameof(points));
        xmax_point* nativePoints = stackalloc xmax_point[points.Length];
        for (int i = 0; i < points.Length; ++i) nativePoints[i] = new() { x = points[i].X, y = points[i].Y };
        var native = new xmax_interaction_frame { struct_size = (uint)sizeof(xmax_interaction_frame), points = nativePoints,
            point_count = (nuint)points.Length, viewport_width = viewportWidth, viewport_height = viewportHeight,
            displayed_video_width = videoWidth, displayed_video_height = videoHeight, content_mode = (int)mode };
        NativeError.Check(NativeMethods.xmax_realtime_submit_interaction(lease.Value, &native));
    }

    /// <summary>
    /// Copies the latest unexpired trajectory points for custom effect rendering.
    /// </summary>
    /// <returns>The normalized video coordinates; an empty list means no unexpired points remain.</returns>
    public unsafe IReadOnlyList<TrajectoryPoint> GetTrajectory()
    {
        using var lease = handle.Borrow();
        xmax_point* points = stackalloc xmax_point[256];
        nuint count = 0;
        NativeError.Check(NativeMethods.xmax_realtime_get_trajectory(lease.Value, points, 256, &count));
        var result = new TrajectoryPoint[checked((int)count)];
        for (int i = 0; i < result.Length; ++i) result[i] = new(points[i].x, points[i].y);
        return result;
    }
}
