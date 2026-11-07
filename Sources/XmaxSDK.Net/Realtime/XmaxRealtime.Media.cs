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
    /// Registers one serialized listener for result-user PCM and generated video after SDK beauty on a shared rendering timeline.
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
    ) => ChangeLocalMediaAsync(() => operations.Run(value => BindExternal(value, format, hasAudio)));

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
    ) => SubmitInteractionCore(points, viewportWidth, viewportHeight, videoWidth, videoHeight, null, mode);

    /// <summary>Submits trajectory samples using the mirror state of the displayed frame.</summary>
    /// <param name="points">Viewport points in sampling order.</param>
    /// <param name="viewportWidth">The viewport width.</param>
    /// <param name="viewportHeight">The viewport height.</param>
    /// <param name="videoWidth">The displayed frame width.</param>
    /// <param name="videoHeight">The displayed frame height.</param>
    /// <param name="mirrored">Whether the viewport displayed horizontally mirrored pixels.</param>
    /// <param name="mode">The displayed video's aspect-ratio layout.</param>
    public unsafe void SubmitInteraction(
        ReadOnlySpan<TrajectoryPoint> points,
        double viewportWidth,
        double viewportHeight,
        uint videoWidth,
        uint videoHeight,
        bool mirrored,
        VideoContentMode mode = VideoContentMode.Fit
    )
        => SubmitInteractionCore(points, viewportWidth, viewportHeight, videoWidth, videoHeight, mirrored, mode);

    private unsafe void SubmitInteractionCore(
        ReadOnlySpan<TrajectoryPoint> points,
        double viewportWidth,
        double viewportHeight,
        uint videoWidth,
        uint videoHeight,
        bool? mirrored,
        VideoContentMode mode,
        ulong remoteRevision = 0
    )
    {
        using var lease = handle.Borrow();
        if (points.Length is < 1 or > 256) throw new ArgumentOutOfRangeException(nameof(points));
        xmax_point* nativePoints = stackalloc xmax_point[points.Length];
        for (int i = 0; i < points.Length; ++i) nativePoints[i] = new() { x = points[i].X, y = points[i].Y };
        var native = new xmax_interaction_frame { struct_size = (uint)sizeof(xmax_interaction_frame), points = nativePoints,
            point_count = (nuint)points.Length, viewport_width = viewportWidth, viewport_height = viewportHeight,
            displayed_video_width = videoWidth, displayed_video_height = videoHeight, content_mode = (int)mode };
        int result;
        if (remoteRevision != 0)
            result = NativeMethods.xmax_realtime_submit_stream_interaction(
                lease.Value, remoteRevision, &native, mirrored == true ? 1 : 0);
        else if (mirrored.HasValue)
            result = NativeMethods.xmax_realtime_submit_interaction_view(lease.Value, &native, mirrored.Value ? 1 : 0);
        else
            result = NativeMethods.xmax_realtime_submit_interaction(lease.Value, &native);
        NativeError.Check(result);
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

    /// <summary>
    /// Reads the current local input or camera encoding format.
    /// </summary>
    /// <returns>A task containing the bound format; camera capture frames may have different dimensions.</returns>
    /// <remarks>
    /// This query may wait for native lifecycle work.
    /// </remarks>
    public Task<VideoFormat> GetFormatAsync() => operations.Run(GetFormat);

    private static unsafe VideoFormat GetFormat(nint value)
    { xmax_video_format format; NativeError.Check(NativeMethods.xmax_realtime_get_format(value, &format)); return VideoFormat.From(format); }

    /// <summary>
    /// Reads the playback volume retained for the current or next connection.
    /// </summary>
    /// <returns>A task containing a volume from 0 through 1, initially 0.</returns>
    /// <remarks>
    /// This query does not wait for native connection or generation admission.
    /// </remarks>
    public Task<float> GetRemoteVolumeAsync() => operations.Run(GetVolume);

    private static unsafe float GetVolume(nint value)
    { float volume; NativeError.Check(NativeMethods.xmax_realtime_get_remote_volume(value, &volume)); return volume; }
}
