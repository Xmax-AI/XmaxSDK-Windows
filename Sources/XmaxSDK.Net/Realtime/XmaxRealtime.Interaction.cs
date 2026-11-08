//
//  XmaxRealtime.Interaction.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

internal sealed partial class XmaxRealtime
{
    internal bool TrySubmitInteraction(
        RealtimeVideoTrack track, ReadOnlySpan<TrajectoryPoint> points, double width, double height,
        uint videoWidth, uint videoHeight, bool mirrored, VideoContentMode mode
    )
    {
        lock (trackGate)
        {
            if (!ReferenceEquals(currentRemoteStream?.VideoTrack, track))
                return false;
            try
            {
                SubmitInteractionCore(points, width, height, videoWidth, videoHeight, mirrored, mode,
                    currentRemoteStream!.Revision);
                return true;
            }
            catch (XmaxException error) when (error.Code == XmaxErrorCode.InvalidState) { return false; }
            catch (ObjectDisposedException) { return false; }
        }
    }

    private unsafe void SubmitInteractionCore(
        ReadOnlySpan<TrajectoryPoint> points,
        double viewportWidth,
        double viewportHeight,
        uint videoWidth,
        uint videoHeight,
        bool mirrored,
        VideoContentMode mode,
        ulong remoteRevision
    )
    {
        using var lease = handle.Borrow();
        if (points.Length is < 1 or > 256) throw new ArgumentOutOfRangeException(nameof(points));
        xmax_point* nativePoints = stackalloc xmax_point[points.Length];
        for (int i = 0; i < points.Length; ++i) nativePoints[i] = new() { x = points[i].X, y = points[i].Y };
        var native = new xmax_interaction_frame { struct_size = (uint)sizeof(xmax_interaction_frame), points = nativePoints,
            point_count = (nuint)points.Length, viewport_width = viewportWidth, viewport_height = viewportHeight,
            displayed_video_width = videoWidth, displayed_video_height = videoHeight, content_mode = (int)mode };
        NativeError.Check(NativeMethods.xmax_realtime_submit_stream_interaction(
            lease.Value, remoteRevision, &native, mirrored ? 1 : 0));
    }
}
