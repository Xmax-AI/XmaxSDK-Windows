//
//  RealtimeVideoTrack.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>A realtime video binding with stable identity and current media metadata.</summary>
public sealed class RealtimeVideoTrack : IXmaxVideoSource
{
    private readonly IXmaxVideoSource source;
    private readonly XmaxRealtime owner;
    private readonly ulong revision;
    private readonly bool cameraPreview;
    private readonly object metadata = new();
    private VideoFormat? format;
    private VideoFormat? observedFormat;
    private CameraDevice? device;
    internal VideoFormat? InteractionFormat { get; set; }
    private long interactionRevision;
    internal long InteractionRevision => Interlocked.Read(ref interactionRevision);
    internal void ResetInteraction() => Interlocked.Increment(ref interactionRevision);

    internal RealtimeVideoTrack(XmaxRealtime owner, string id, bool remote, IXmaxVideoSource source,
        ulong revision, bool cameraPreview)
    {
        this.owner = owner;
        this.source = source;
        this.revision = revision;
        this.cameraPreview = cameraPreview;
        Id = id;
        IsRemote = remote;
    }

    /// <summary>Gets the identifier of this track within its realtime controller.</summary>
    public string Id { get; }

    /// <summary>Indicates whether the track carries the generated result.</summary>
    public bool IsRemote { get; }

    /// <summary>Gets the current input format or the negotiated result format.</summary>
    public VideoFormat? VideoFormat { get { lock (metadata) return format; } }

    /// <summary>Gets the most recent decoded result format, when available.</summary>
    public VideoFormat? ObservedVideoFormat { get { lock (metadata) return observedFormat; } }

    /// <summary>Gets the selected camera, when the track represents a camera.</summary>
    public CameraDevice? Device { get { lock (metadata) return device; } }

    /// <summary>
    /// Gets the camera's enclosure position, or null for non-camera tracks.
    /// </summary>
    public CameraPosition? Position => Device?.Position;

    internal void UpdateFormat(VideoFormat? value) { lock (metadata) format = value; }
    internal void UpdateDevice(CameraDevice? value) { lock (metadata) device = value; }

    /// <inheritdoc />
    public XmaxVideoFrame? TryAcquireFrame()
    {
        var frame = source.TryAcquireFrame();
        if (frame is not null && IsRemote)
            ObserveFrame(frame.Width, frame.Height);
        return frame;
    }

    /// <summary>Acquires a frame and reports when the result binding invalidated previously presented pixels.</summary>
    /// <param name="contentRevision">The current content revision, including when no frame is available.</param>
    /// <returns>The latest owned frame, or null when no frame is pending.</returns>
    public XmaxVideoFrame? TryAcquireFrame(out ulong contentRevision)
    {
        contentRevision = 0;
        if (!IsRemote)
            return TryAcquireFrame();
        var frame = owner.AcquireResultFrame(revision, out contentRevision);
        if (frame is not null)
            ObserveFrame(frame.Width, frame.Height);
        return frame;
    }

    /// <summary>Confirms that a custom preview renderer is attached to the camera track.</summary>
    /// <remarks>SDK video views report attachment automatically. Connection can start independently.</remarks>
    public Task NotifyPreviewAttachedAsync() => cameraPreview
        ? owner.CompleteLocalCameraPreviewAsync(revision)
        : Task.CompletedTask;

    /// <summary>Reports attachment from a renderer that observes the first presented camera frame.</summary>
    public Task NotifyPreviewPresentedAsync() => NotifyPreviewAttachedAsync();

    internal void ObserveFrame(uint width, uint height, uint? framesPerSecond = null)
    {
        lock (metadata)
            observedFormat = new(width, height, framesPerSecond ?? observedFormat?.FramesPerSecond ?? 0);
    }

    internal void ClearObservedFormat() { lock (metadata) observedFormat = null; }

    /// <summary>
    /// Queues sampled pointer locations for the current generation task.
    /// </summary>
    /// <param name="points">One to 256 locations in viewport units.</param>
    /// <param name="viewportWidth">The positive width of the viewport.</param>
    /// <param name="viewportHeight">The positive height of the viewport.</param>
    /// <param name="videoWidth">The displayed video's width, in pixels.</param>
    /// <param name="videoHeight">The displayed video's height, in pixels.</param>
    /// <param name="mirrored">Whether the displayed frame is horizontally mirrored.</param>
    /// <param name="mode">The aspect ratio layout used to display the video.</param>
    /// <returns>Whether the active result binding accepted the sample.</returns>
    public bool TrySubmitInteraction(
        ReadOnlySpan<TrajectoryPoint> points,
        double viewportWidth,
        double viewportHeight,
        uint videoWidth,
        uint videoHeight,
        bool mirrored,
        VideoContentMode mode = VideoContentMode.Fit
    ) => IsRemote && owner.TrySubmitInteraction(
        this,
        points,
        viewportWidth,
        viewportHeight,
        videoWidth,
        videoHeight,
        mirrored,
        mode
    );
}
