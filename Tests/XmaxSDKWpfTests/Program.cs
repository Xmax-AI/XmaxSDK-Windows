//
//  Program.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Xmax.SDK;
using Xmax.SDK.Wpf;
using Xmax.SDK.Rendering;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        int result = 1;
        app.Startup += async (_, _) =>
        {
            try { await VerifyAsync(); await VerifyDualAsync(); result = 0; }
            catch (Exception error) { Console.Error.WriteLine(error); }
            finally { app.Shutdown(result); }
        };
        app.Run();
        return result;
    }
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static async Task WaitAsync(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow.AddSeconds(8);
        while (!condition()) { if (DateTime.UtcNow >= deadline) throw new TimeoutException("WPF frame delivery timed out."); await Task.Delay(15); }
    }
    private static async Task VerifyAsync()
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtimeManager();
        var source = new TestSource();
        var stream = await realtime.CreateExternalVideoStreamAsync(source);
        var sink = source.Sink!;
        var view = new XmaxRealtimeVideoView { Width = 160, Height = 80, Track = stream.VideoTrack, FirstFrameFadeInDuration = 0 };
        var host = new Grid { Width = 160, Height = 80, Background = Brushes.Black };
        host.Children.Add(view);
        view.Visibility = Visibility.Collapsed;
        var window = new Window { Content = host, SizeToContent = SizeToContent.WidthAndHeight, WindowStyle = WindowStyle.None };
        Exception? displayError = null;
        view.PlaybackFailed += error => displayError = error;
        window.Show();
        byte[] pixels = new byte[TestSource.Stride * TestSource.Height];
        for (int y = 0; y < TestSource.Height; ++y)
            for (int x = 0; x < TestSource.Width; ++x)
            { int i = y * TestSource.Stride + x * 4; pixels[i + (x < TestSource.Width / 2 ? 2 : 0)] = 255; pixels[i + 3] = 255; }
        sink.PushBgra(pixels, TestSource.Stride, 1000);
        await WaitAsync(() => view.PresentedFrameCount >= 1);
        view.Visibility = Visibility.Visible;
        await Task.Delay(50);
        Check(displayError is null && view.VideoWidth == TestSource.Width && view.VideoHeight == TestSource.Height, "BGRA display metadata");
        Check(Pixel(view, 20, 40).R > 200 && Pixel(view, 140, 40).B > 200, "displayed pixels differ from input");
        Console.WriteLine("PASS WPF displayed real SDK-owned BGRA pixels");

        view.IsMirrored = true;
        Check(Pixel(view, 20, 40).B > 200 && Pixel(view, 140, 40).R > 200, "view mirroring redraws the current frame");
        sink.PushBgra(pixels, TestSource.Stride, 2000);
        await WaitAsync(() => view.PresentedFrameCount >= 2);
        await Task.Delay(50);
        Check(Pixel(view, 20, 40).B > 200 && Pixel(view, 140, 40).R > 200, "view mirroring persists across frames");
        Console.WriteLine("PASS WPF view mirroring persists across frames");

        var renderer = new TestRenderer();
        view.TrajectoryRenderer = renderer;
        Check(Pixel(view, 4, 4).G > 200 && Pixel(view, 20, 40).B > 200, "custom effect overlays the video");
        view.TrajectoryRenderer = null;
        Check(renderer.Resets == 1 && VisualTreeHelper.GetParent(renderer.View) is null &&
            Pixel(view, 4, 4).B > 200, "removing the effect restores the video and detaches its view");
        Console.WriteLine("PASS WPF custom trajectory effect mounts and detaches above video");

        // Flood faster than display: the final image must be current, without one Dispatcher job per frame.
        await Task.Run(() => { for (int i = 3; i <= 100; ++i) sink.PushBgra(pixels, TestSource.Stride, i * 1000); });
        await WaitAsync(() => view.LastPresentedTimestampMicroseconds == 100000);
        window.Content = null;
        await Task.Delay(50);
        window.Content = host;
        sink.PushBgra(pixels, TestSource.Stride, 101000);
        await WaitAsync(() => view.LastPresentedTimestampMicroseconds == 101000);
        Console.WriteLine("PASS frame replacement and unload/reload");
        view.Track = null;
        await realtime.DisposeAsync();
        window.Close();
        Check(displayError is null, "close raced with the renderer");
        Console.WriteLine("PASS WPF shutdown drains frame resources");
    }
    private static async Task VerifyDualAsync()
    {
        using var client = new XmaxClient();
        await using var local = client.CreateRealtimeManager();
        await using var remote = client.CreateRealtimeManager();
        var preview = new TestSource();
        var result = new TestSource();
        var localStream = await local.CreateExternalVideoStreamAsync(preview);
        var remoteStream = await remote.CreateExternalVideoStreamAsync(result);
        var view = new XmaxRealtimeDualVideoView
        {
            Width = 160, Height = 80, LocalTrack = localStream.VideoTrack, RemoteTrack = remoteStream.VideoTrack,
            RemoteVideoFadeInDuration = 0.6
        };
        Check(view.ContentMode == VideoContentMode.Fill && new XmaxRealtimeVideoView().ContentMode == VideoContentMode.Fill,
            "single and dual controls default to Fill");
        var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight, WindowStyle = WindowStyle.None };
        window.Show();
        try
        {
            preview.Push(2, 1000);
            await WaitAsync(() => Pixel(view, 80, 40).R > 200);
            result.Push(0, 1000);
            var remoteView = (XmaxRealtimeVideoView)view.Children[1];
            if (SystemParameters.ClientAreaAnimation) await WaitAsync(() => remoteView.Opacity is > 0 and < 1);
            await WaitAsync(() => Pixel(view, 80, 40).B > 200);
            preview.Push(1, 2000);
            await WaitAsync(() => ((XmaxRealtimeVideoView)view.Children[0]).LastPresentedTimestampMicroseconds == 2000);
            view.RemoteTrack = null;
            Check(Pixel(view, 80, 40).G > 200, "clearing the result immediately reveals the latest local preview");
            view.RemoteTrack = remoteStream.VideoTrack;
            Check(Pixel(view, 80, 40).G > 200, "a replacement result stays hidden before it presents another frame");
            Console.WriteLine("PASS WPF dual view keeps local preview through first frame, replacement and clearing");
        }
        finally { window.Close(); }
    }

    private sealed class TestSource : IRealtimeExternalVideoSource
    {
        internal const int Width = 640;
        internal const int Height = 320;
        internal const int Stride = Width * 4;
        internal IRealtimeExternalFrameSink? Sink { get; private set; }
        public VideoFormat VideoFormat => new(Width, Height);
        public Task StartAsync(IRealtimeExternalFrameSink value, CancellationToken cancellationToken)
        {
            Sink = value;
            return Task.CompletedTask;
        }
        public Task StopAsync() { Sink = null; return Task.CompletedTask; }
        public void Push(int channel, long timestamp)
        {
            byte[] pixels = new byte[Stride * Height];
            for (int i = 0; i < pixels.Length; i += 4)
            {
                pixels[i + channel] = 255;
                pixels[i + 3] = 255;
            }
            Sink!.PushBgra(pixels, Stride, timestamp);
        }
    }

    private sealed class TestRenderer : IXmaxTrajectoryEffectRenderer
    {
        public int Resets { get; private set; }

        public UIElement View { get; } = new Border
        {
            Background = Brushes.Lime,
            Width = 8,
            Height = 8,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top
        };

        public void RenderBegan(TrajectoryEffectSample sample) { }
        public void RenderMoved(TrajectoryEffectSample sample) { }
        public void RenderEnded(Guid id) { }
        public void Reset() => ++Resets;
    }

    private static Color Pixel(FrameworkElement view, int x, int y)
    {
        view.UpdateLayout();
        var bitmap = new RenderTargetBitmap(160, 80, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(view);
        byte[] pixel = new byte[4];
        bitmap.CopyPixels(new Int32Rect(x, y, 1, 1), pixel, 4, 0);
        return Color.FromArgb(pixel[3], pixel[2], pixel[1], pixel[0]);
    }
}
