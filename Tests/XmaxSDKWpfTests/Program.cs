using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Xmax.SDK;
using Xmax.SDK.Wpf;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        int result = 1;
        app.Startup += async (_, _) =>
        {
            try { await VerifyAsync(); result = 0; }
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
        await using var realtime = client.CreateRealtime();
        using var sink = await realtime.BindExternalAsync(new(16, 8));
        var view = new XmaxRealtimeVideoView { Width = 160, Height = 80, VideoSource = realtime.LocalVideo };
        var window = new Window { Content = view, SizeToContent = SizeToContent.WidthAndHeight, WindowStyle = WindowStyle.None };
        Exception? displayError = null;
        view.PlaybackFailed += error => displayError = error;
        window.Show();
        byte[] pixels = new byte[64 * 8];
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
            { int i = y * 64 + x * 4; pixels[i + (x < 8 ? 2 : 0)] = 255; pixels[i + 3] = 255; }
        sink.PushBgra(pixels, 64, 1000);
        await WaitAsync(() => view.PresentedFrameCount >= 1);
        await Task.Delay(50);
        Check(displayError is null && view.VideoWidth == 16 && view.VideoHeight == 8, "BGRA display metadata");
        Check(Pixel(view, 20, 40).R > 200 && Pixel(view, 140, 40).B > 200, "displayed pixels differ from input");
        Console.WriteLine("PASS WPF displayed real SDK-owned BGRA pixels");

        await realtime.SetVideoMirrorAsync(new(true, false, false));
        sink.PushBgra(pixels, 64, 2000);
        await WaitAsync(() => view.PresentedFrameCount >= 2);
        await Task.Delay(50);
        Check(Pixel(view, 20, 40).B > 200 && Pixel(view, 140, 40).R > 200, "mirror metadata applied once");
        Console.WriteLine("PASS WPF mirror metadata is applied once");

        // Flood faster than display: the final image must be current, without one Dispatcher job per frame.
        await Task.Run(() => { for (int i = 3; i <= 100; ++i) sink.PushBgra(pixels, 64, i * 1000); });
        await WaitAsync(() => view.LastPresentedTimestampMicroseconds == 100000);
        window.Content = null;
        await Task.Delay(50);
        window.Content = view;
        sink.PushBgra(pixels, 64, 101000);
        await WaitAsync(() => view.LastPresentedTimestampMicroseconds == 101000);
        Console.WriteLine("PASS frame replacement and unload/reload");
        view.VideoSource = null;
        await realtime.DisposeAsync();
        window.Close();
        Check(displayError is null, "close raced with the renderer");
        Console.WriteLine("PASS WPF shutdown drains frame resources");
    }
    private static Color Pixel(XmaxRealtimeVideoView view, int x, int y)
    {
        view.UpdateLayout();
        var bitmap = new RenderTargetBitmap(160, 80, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(view);
        byte[] pixel = new byte[4];
        bitmap.CopyPixels(new Int32Rect(x, y, 1, 1), pixel, 4, 0);
        return Color.FromArgb(pixel[3], pixel[2], pixel[1], pixel[0]);
    }
}
