using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;


public sealed partial class XmaxRealtime
{
    /// <summary>Runs on the SDK delivery thread. Each callback owns its packet and must dispose it.
    /// Replacement releases the previous context only after its in-flight callbacks finish.</summary>
    public Task SetRemoteMediaListenerAsync(Action<XmaxMediaFrame>? listener) =>
        operations.Run(value => { RemoteMediaRegistration.Set(value, listener, NotifyCallbackFault); return 0; });
    public Task<ExternalFrameSink> BindExternalAsync(VideoFormat format, bool hasAudio = false) => operations.Run(value => BindExternal(value, format, hasAudio));
    private static unsafe ExternalFrameSink BindExternal(nint value, VideoFormat format, bool hasAudio)
    {
        var native = new xmax_external_media_config { struct_size = (uint)sizeof(xmax_external_media_config), video_format = format.Native, has_audio = hasAudio ? 1 : 0 };
        nint sink = 0;
        NativeError.Check(NativeMethods.xmax_realtime_bind_external_media(value, &native, &sink));
        return new(new(sink, HandleKind.ExternalSink), format);
    }
    public unsafe void SubmitInteraction(ReadOnlySpan<TrajectoryPoint> points, double viewportWidth, double viewportHeight,
        uint videoWidth, uint videoHeight, VideoContentMode mode = VideoContentMode.Fit)
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
