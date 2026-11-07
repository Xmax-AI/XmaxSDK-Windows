//
//  XmaxRealtime.Tracks.cs
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
    internal unsafe XmaxVideoFrame? AcquireResultFrame(ulong revision, out ulong contentRevision)
    {
        contentRevision = 0;
        if (handle.IsClosed) return null;
        using var lease = handle.Borrow();
        nint frame = 0;
        ulong content = 0;
        NativeError.Check(NativeMethods.xmax_realtime_acquire_result_video_frame(lease.Value, revision, &content, &frame));
        contentRevision = content;
        return frame == 0 ? null : new(new NativeHandle(frame, HandleKind.Frame));
    }

    private readonly object trackGate = new();
    private RealtimeMediaStream? currentLocalStream;
    private RealtimeMediaStream? currentRemoteStream;

    private RealtimeMediaStream CreateRemoteStream(nint value, ulong revision)
    {
        if (Volatile.Read(ref currentRemoteStream) is { } current && current.Revision == revision)
            return current;

        var format = ReadResultTrackFormat(value, revision);
        lock (trackGate)
        {
            if (currentRemoteStream?.Revision == revision)
                return currentRemoteStream;
            var stream = new RealtimeMediaStream(this, false, revision, new VideoSource(handle, false, revision));
            stream.VideoTrack.InteractionFormat = currentLocalStream?.VideoTrack.VideoFormat;
            stream.VideoTrack.UpdateFormat(format);
            if (currentRemoteStream is null || currentRemoteStream.Revision < revision)
                Volatile.Write(ref currentRemoteStream, stream);
            return stream;
        }
    }

    private static unsafe VideoFormat? ReadResultTrackFormat(nint value, ulong revision)
    {
        xmax_video_format format;
        var encoding = new xmax_encoding_config { struct_size = (uint)sizeof(xmax_encoding_config) };
        int result = NativeMethods.xmax_realtime_get_video_track_info(value, 0, revision, &format, &encoding, null);
        NativeError.Check(result);
        return result == 0 ? VideoFormat.From(format).WithEncoding(encoding) : null;
    }

    private void RefreshResultTrackFormat(nint value)
    {
        if (Volatile.Read(ref currentRemoteStream) is { } stream && ReadResultTrackFormat(value, stream.Revision) is { } format)
            stream.VideoTrack.UpdateFormat(format);
    }

    private unsafe RealtimeMediaStream CreateLocalStream(
        nint value, bool cameraPreview = false, string? cameraDeviceId = null, bool isCamera = false
    )
    {
        ulong revision;
        NativeError.Check(NativeMethods.xmax_realtime_get_local_stream_revision(value, &revision));
        CameraDevice? device = null;
        if (cameraPreview || isCamera)
        {
            try
            {
                var devices = EnumerateCameraDevices(value);
                device = string.IsNullOrEmpty(cameraDeviceId)
                    ? devices.FirstOrDefault(item => item.IsDefault) ?? devices.FirstOrDefault()
                    : devices.FirstOrDefault(item => item.Id == cameraDeviceId);
            }
            catch (XmaxException) { /* Metadata discovery must not invalidate prepared capture. */ }
        }
        var format = ReadLocalFormat(value);
        lock (trackGate)
        {
            if (currentLocalStream?.Revision != revision)
                currentLocalStream = new(this, true, revision, new VideoSource(handle, true, revision), cameraPreview);
            currentLocalStream.VideoTrack.UpdateFormat(format);
            currentLocalStream.VideoTrack.UpdateDevice(device);
            return currentLocalStream;
        }
    }

    private static unsafe VideoFormat? ReadLocalFormat(nint value)
    {
        xmax_video_format format;
        NativeError.Check(NativeMethods.xmax_realtime_get_format(value, &format));
        return format.width == 0 ? null : VideoFormat.From(format).WithEncoding(ReadEncoding(value));
    }

    private static unsafe xmax_encoding_config ReadEncoding(nint value)
    {
        var encoding = new xmax_encoding_config { struct_size = (uint)sizeof(xmax_encoding_config) };
        NativeError.Check(NativeMethods.xmax_realtime_get_encoding(value, &encoding));
        return encoding;
    }

    private unsafe void RefreshLocalTrackFormat(nint value)
    {
        ulong revision;
        NativeError.Check(NativeMethods.xmax_realtime_get_local_stream_revision(value, &revision));
        var format = ReadLocalFormat(value);
        lock (trackGate)
            if (currentLocalStream?.Revision == revision)
                currentLocalStream.VideoTrack.UpdateFormat(format);
    }

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
            catch (XmaxException error) when (error.Result == -2) { return false; }
            catch (ObjectDisposedException) { return false; }
        }
    }
}
