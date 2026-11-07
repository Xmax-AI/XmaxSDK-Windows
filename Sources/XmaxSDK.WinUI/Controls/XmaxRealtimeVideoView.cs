//
//  XmaxRealtimeVideoView.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.IO;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Xmax.SDK.Rendering;

namespace Xmax.SDK.WinUI;

/// <summary>
/// A WinUI control displaying the latest SDK-owned BGRA frame with aspect-ratio layout and mirror metadata.
/// </summary>
/// <remarks>
/// Create and access this control on its UI thread. Assign only one independent consumer to each video source.
/// Unloading or replacing VideoSource stops the old frame pump; your app still owns and disposes the realtime session.
/// </remarks>
public sealed partial class XmaxRealtimeVideoView : Grid
{

    /// <summary>
    /// Identifies the VideoSource dependency property.
    /// </summary>
    public static readonly DependencyProperty VideoSourceProperty = DependencyProperty.Register(nameof(VideoSource), typeof(IXmaxVideoSource),
        typeof(XmaxRealtimeVideoView), new PropertyMetadata(null, (owner, _) => ((XmaxRealtimeVideoView)owner).Restart()));

    /// <summary>
    /// Identifies the ContentMode dependency property.
    /// </summary>
    public static readonly DependencyProperty ContentModeProperty = DependencyProperty.Register(nameof(ContentMode), typeof(VideoContentMode),
        typeof(XmaxRealtimeVideoView), new PropertyMetadata(VideoContentMode.Fill, (owner, _) => ((XmaxRealtimeVideoView)owner).UpdateLayoutMode()));

    private readonly Image image = new() { HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Stretch };

    private readonly ScaleTransform mirrorTransform = new();
    private bool frameMirrored;
    private bool isMirrored;

    private WriteableBitmap? bitmap;

    private Stream? pixels;

    private VideoFramePump? pump;
    private ulong contentRevision;
    private readonly XmaxTrajectoryOverlay trajectory;

    /// <summary>Gets or sets whether primary pointer gestures are enabled on result video.</summary>
    public bool IsInteractionEnabled { get => trajectory.InteractionEnabled; set => trajectory.InteractionEnabled = value; }

    /// <summary>
    /// Gets or sets the renderer for the displayed trajectory effect. Null restores the SDK effect.
    /// </summary>
    public IXmaxTrajectoryEffectRenderer? TrajectoryRenderer
    {
        get => trajectory.TrajectoryRenderer;
        set => trajectory.TrajectoryRenderer = value;
    }

    /// <summary>
    /// Gets or sets the local or remote single-consumer video source; null stops frame acquisition.
    /// </summary>
    /// <remarks>
    /// Replacing the source resets presentation counters and dimensions.
    /// </remarks>
    public IXmaxVideoSource? VideoSource
    {
        get => (IXmaxVideoSource?)GetValue(VideoSourceProperty);
        set => SetValue(VideoSourceProperty, value);
    }

    /// <summary>
    /// Gets or sets aspect-fit or aspect-fill layout; the default is Fill.
    /// </summary>
    public VideoContentMode ContentMode
    {
        get => (VideoContentMode)GetValue(ContentModeProperty);
        set => SetValue(ContentModeProperty, value);
    }

    /// <summary>
    /// Gets the number of frames presented since the source was last restarted.
    /// </summary>
    public long PresentedFrameCount { get; private set; }

    /// <summary>
    /// Gets the most recently presented source timestamp, in microseconds; initially 0.
    /// </summary>
    public long LastPresentedTimestampMicroseconds { get; private set; }

    /// <summary>
    /// Gets the most recently presented frame width, in pixels; 0 before a frame is presented.
    /// </summary>
    public uint VideoWidth { get; private set; }

    /// <summary>
    /// Gets the most recently presented frame height, in pixels; 0 before a frame is presented.
    /// </summary>
    public uint VideoHeight { get; private set; }

    /// <summary>
    /// Occurs on the UI thread when frame acquisition or presentation fails.
    /// </summary>
    public event Action<Exception>? PlaybackFailed;

    /// <summary>Mirrors this view and maps interactions back to unmirrored video coordinates.</summary>
    public bool IsMirrored
    {
        get => isMirrored;
        set
        {
            if (isMirrored == value) return;
            isMirrored = value;
            ApplyDisplayMirror();
        }
    }

    /// <summary>
    /// Creates an unbound video control with a black background.
    /// </summary>
    public XmaxRealtimeVideoView()
    {
        Background = new SolidColorBrush(Colors.Black);
        image.RenderTransformOrigin = new(.5, .5);
        image.RenderTransform = mirrorTransform;
        Children.Add(image);
        trajectory = new(this);
        UpdateLayoutMode();
        Loaded += (_, _) => Restart();
        Unloaded += (_, _) =>
        {
            Stop();
            ResetFirstFramePresentation();
        };
    }

    private void UpdateLayoutMode()
    {
        image.Stretch = ContentMode == VideoContentMode.Fit ? Stretch.Uniform : Stretch.UniformToFill;
        ApplyDisplayMirror();
    }

    private void ApplyDisplayMirror()
    {
        bool mirrored = IsMirrored || frameMirrored;
        mirrorTransform.ScaleX = mirrored ? -1 : 1;
        trajectory.UpdateFrame(VideoWidth, VideoHeight, mirrored, ContentMode);
    }

    private void Restart()
    {
        Stop();
        trajectory.Track = VideoSource as RealtimeVideoTrack;
        contentRevision = 0;
        ClearPresentation();
        if (VideoSource is RealtimeVideoTrack { IsRemote: false } track)
            _ = ConfirmPreviewAsync(track);
        if (!IsLoaded || VideoSource is null) return;
        pump = new(VideoSource, error => DispatcherQueue.TryEnqueue(() => PlaybackFailed?.Invoke(error)));
        CompositionTarget.Rendering += RenderFrame;
    }

    private void ClearPresentation()
    {
        image.Source = null; bitmap = null;
        pixels?.Dispose(); pixels = null;
        VideoWidth = VideoHeight = 0; PresentedFrameCount = 0; LastPresentedTimestampMicroseconds = 0;
        frameMirrored = false;
        ApplyDisplayMirror();
        ResetFirstFramePresentation();
    }

    private void Stop()
    {
        CompositionTarget.Rendering -= RenderFrame;
        var previous = pump; pump = null;
        if (previous is not null) _ = previous.DisposeAsync();
    }

    private unsafe void RenderFrame(object? sender, object args)
    {
        if (pump is null) return;
        using var frame = pump.Frames.Take(out var revision);
        if (revision != contentRevision)
        {
            contentRevision = revision;
            ClearPresentation();
        }
        if (frame is null) return;
        try
        {
            int width = checked((int)frame.Width), height = checked((int)frame.Height), rowBytes = checked(width * 4);
            if (bitmap is null || bitmap.PixelWidth != width || bitmap.PixelHeight != height)
            {
                pixels?.Dispose();
                bitmap = new(width, height); pixels = bitmap.PixelBuffer.AsStream(); image.Source = bitmap;
            }
            pixels!.Position = 0;
            frame.ReadBgra((data, stride, _) =>
            {
                if (stride == rowBytes) pixels.Write(new ReadOnlySpan<byte>((void*)data, checked(rowBytes * height)));
                else for (int y = 0; y < height; ++y) pixels.Write(new ReadOnlySpan<byte>((byte*)data + y * stride, rowBytes));
            });
            bitmap.Invalidate();
            frameMirrored = frame.Mirrored;
            VideoWidth = frame.Width; VideoHeight = frame.Height;
            ApplyDisplayMirror();
            LastPresentedTimestampMicroseconds = frame.TimestampMicroseconds;
            if (++PresentedFrameCount == 1) PresentFirstFrame();
        }
        catch (Exception error) { Stop(); PlaybackFailed?.Invoke(error); }
    }
}
