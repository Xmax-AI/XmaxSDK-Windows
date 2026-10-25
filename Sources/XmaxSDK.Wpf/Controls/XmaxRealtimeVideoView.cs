using System.Windows;
using System.Windows.Media;
using Xmax.SDK.Wpf.Rendering;
using Xmax.SDK.Rendering;

namespace Xmax.SDK.Wpf;

/// <summary>Latest-frame BGRA display with native mirror metadata and fit/fill layout.</summary>
public sealed class XmaxRealtimeVideoView : FrameworkElement
{
    public static readonly DependencyProperty VideoSourceProperty = DependencyProperty.Register(nameof(VideoSource), typeof(IXmaxVideoSource),
        typeof(XmaxRealtimeVideoView), new PropertyMetadata(null, (owner, _) => ((XmaxRealtimeVideoView)owner).Restart()));
    public static readonly DependencyProperty ContentModeProperty = DependencyProperty.Register(nameof(ContentMode), typeof(VideoContentMode),
        typeof(XmaxRealtimeVideoView), new FrameworkPropertyMetadata(VideoContentMode.Fit, FrameworkPropertyMetadataOptions.AffectsRender));
    private readonly WriteableBitmapPresenter presenter = new();
    private VideoFramePump? pump;
    private bool mirrored;
    private bool rendering;
    public IXmaxVideoSource? VideoSource { get => (IXmaxVideoSource?)GetValue(VideoSourceProperty); set => SetValue(VideoSourceProperty, value); }
    public VideoContentMode ContentMode { get => (VideoContentMode)GetValue(ContentModeProperty); set => SetValue(ContentModeProperty, value); }
    public long PresentedFrameCount { get; private set; }
    public long LastPresentedTimestampMicroseconds { get; private set; }
    public uint VideoWidth { get; private set; }
    public uint VideoHeight { get; private set; }
    public event Action<Exception>? PlaybackFailed;
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
    protected override void OnRender(DrawingContext drawing)
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
