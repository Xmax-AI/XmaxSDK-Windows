//
//  XmaxRealtimeVideoView.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Windows;
using System.Windows.Media;
using Xmax.SDK.Wpf.Rendering;
using Xmax.SDK.Rendering;

namespace Xmax.SDK.Wpf;

/// <summary>
/// A Wpf control displaying the latest SDK-owned BGRA frame with aspect-ratio layout and mirror metadata.
/// </summary>
/// <remarks>
/// Create and access this control on its UI thread. Assign only one independent consumer to each video source.
/// Unloading or replacing VideoSource stops the old frame pump; your app still owns and disposes the realtime session.
/// </remarks>
public sealed class XmaxRealtimeVideoView : FrameworkElement
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
        typeof(XmaxRealtimeVideoView), new FrameworkPropertyMetadata(VideoContentMode.Fit, FrameworkPropertyMetadataOptions.AffectsRender));

    private readonly WriteableBitmapPresenter presenter = new();

    private VideoFramePump? pump;

    private bool mirrored;

    private bool rendering;

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
    /// Gets or sets aspect-fit or aspect-fill layout; the default is Fit.
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

    /// <summary>
    /// Creates an unbound video control with a black background.
    /// </summary>
    public XmaxRealtimeVideoView()
    {
        ClipToBounds = true;
        Loaded += (_, _) => Restart();
        Unloaded += (_, _) => Stop();
    }

    private void Restart()
    {
        Stop();
        presenter.Clear();
        VideoWidth = VideoHeight = 0;
        PresentedFrameCount = 0;
        LastPresentedTimestampMicroseconds = 0;
        InvalidateVisual();
        if (!IsLoaded || VideoSource is null) return;
        pump = new(VideoSource, error => Dispatcher.InvokeAsync(() => PlaybackFailed?.Invoke(error)));
        CompositionTarget.Rendering += RenderFrame;
    }

    private void Stop()
    {
        CompositionTarget.Rendering -= RenderFrame;
        var previous = pump;
        pump = null;
        if (previous is not null) _ = previous.DisposeAsync();
    }

    private void RenderFrame(object? sender, EventArgs args)
    {
        if (rendering) return;
        using var frame = pump?.Frames.Take();
        if (frame is null) return;
        rendering = true;
        try
        {
            presenter.Present(frame);
            mirrored = frame.Mirrored;
            VideoWidth = frame.Width; VideoHeight = frame.Height;
            ++PresentedFrameCount;
            LastPresentedTimestampMicroseconds = frame.TimestampMicroseconds;
            InvalidateVisual();
        }
        catch (Exception error) { Stop(); PlaybackFailed?.Invoke(error); }
        finally { rendering = false; }
    }

    /// <summary>
    /// Draws the latest frame using the selected content mode and frame mirror metadata.
    /// </summary>
    /// <param name="drawing">The UI-thread drawing context supplied by WPF.</param>
    protected override void OnRender(
        DrawingContext drawing
    )
    {
        drawing.DrawRectangle(Brushes.Black, null, new Rect(RenderSize));
        var image = presenter.Bitmap;
        if (image is null || ActualWidth <= 0 || ActualHeight <= 0) return;
        double scale = ContentMode == VideoContentMode.Fit ? Math.Min(ActualWidth / image.PixelWidth, ActualHeight / image.PixelHeight)
            : Math.Max(ActualWidth / image.PixelWidth, ActualHeight / image.PixelHeight);
        double width = image.PixelWidth * scale, height = image.PixelHeight * scale;
        var rect = new Rect((ActualWidth - width) / 2, (ActualHeight - height) / 2, width, height);
        if (mirrored) drawing.PushTransform(new ScaleTransform(-1, 1, ActualWidth / 2, 0));
        drawing.DrawImage(image, rect);
        if (mirrored) drawing.Pop();
    }
}
