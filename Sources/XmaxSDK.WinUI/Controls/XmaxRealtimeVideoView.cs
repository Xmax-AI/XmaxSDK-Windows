using System.IO;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Xmax.SDK.Rendering;

namespace Xmax.SDK.WinUI;

/// <summary>Native WinUI 3 video display. Keeps only the latest SDK-owned BGRA frame.</summary>
public sealed class XmaxRealtimeVideoView : Grid
{
    public static readonly DependencyProperty VideoSourceProperty = DependencyProperty.Register(nameof(VideoSource), typeof(IXmaxVideoSource),
        typeof(XmaxRealtimeVideoView), new PropertyMetadata(null, (owner, _) => ((XmaxRealtimeVideoView)owner).Restart()));
    public static readonly DependencyProperty ContentModeProperty = DependencyProperty.Register(nameof(ContentMode), typeof(VideoContentMode),
        typeof(XmaxRealtimeVideoView), new PropertyMetadata(VideoContentMode.Fit, (owner, _) => ((XmaxRealtimeVideoView)owner).UpdateLayoutMode()));
    private readonly Image image = new() { HorizontalAlignment = HorizontalAlignment.Stretch, VerticalAlignment = VerticalAlignment.Stretch };
    private WriteableBitmap? bitmap;
    private Stream? pixels;
    private VideoFramePump? pump;
    public IXmaxVideoSource? VideoSource { get => (IXmaxVideoSource?)GetValue(VideoSourceProperty); set => SetValue(VideoSourceProperty, value); }
    public VideoContentMode ContentMode { get => (VideoContentMode)GetValue(ContentModeProperty); set => SetValue(ContentModeProperty, value); }
    public long PresentedFrameCount { get; private set; }
    public long LastPresentedTimestampMicroseconds { get; private set; }
    public uint VideoWidth { get; private set; }
    public uint VideoHeight { get; private set; }
    public event Action<Exception>? PlaybackFailed;
    public XmaxRealtimeVideoView()
    {
        Background = new SolidColorBrush(Colors.Black);
        Children.Add(image);
        UpdateLayoutMode();
        Loaded += (_, _) => Restart();
        Unloaded += (_, _) => Stop();
    }
    private void UpdateLayoutMode() => image.Stretch = ContentMode == VideoContentMode.Fit ? Stretch.Uniform : Stretch.UniformToFill;
    private void Restart()
    {
        Stop();
        image.Source = null; bitmap = null;
        pixels?.Dispose(); pixels = null;
        VideoWidth = VideoHeight = 0; PresentedFrameCount = 0; LastPresentedTimestampMicroseconds = 0;
        if (!IsLoaded || VideoSource is null) return;
        pump = new(VideoSource, error => DispatcherQueue.TryEnqueue(() => PlaybackFailed?.Invoke(error)));
        CompositionTarget.Rendering += RenderFrame;
    }
    private void Stop()
    {
        CompositionTarget.Rendering -= RenderFrame;
        var previous = pump; pump = null;
        if (previous is not null) _ = previous.DisposeAsync();
    }
    private unsafe void RenderFrame(object? sender, object args)
    {
        using var frame = pump?.Frames.Take();
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
            image.RenderTransformOrigin = new(.5, .5);
            image.RenderTransform = new ScaleTransform { ScaleX = frame.Mirrored ? -1 : 1 };
            VideoWidth = frame.Width; VideoHeight = frame.Height;
            LastPresentedTimestampMicroseconds = frame.TimestampMicroseconds;
            ++PresentedFrameCount;
        }
        catch (Exception error) { Stop(); PlaybackFailed?.Invoke(error); }
    }
}
