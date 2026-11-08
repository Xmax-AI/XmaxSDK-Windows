//
//  MainWindow.Verification.Frames.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Storage.Streams;
using Xmax.SDK;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    private async Task VerifyFramesAsync()
    {
        using var client = new XmaxClient(); await using var realtime = client.CreateRealtimeManager();
        var source = new FrameVerificationSource();
        var stream = await realtime.CreateExternalVideoStreamAsync(source);
        var sink = source.Sink!;
        Model.StatisticsVisible = false;
        LocalView.Width = 160; LocalView.Height = 80; LocalView.FirstFrameFadeInDuration = 0;
        LocalView.Track = stream.VideoTrack;
        byte[] pixels = new byte[FrameVerificationSource.Stride * FrameVerificationSource.Height];
        for (int y = 0; y < FrameVerificationSource.Height; ++y) for (int x = 0; x < FrameVerificationSource.Width; ++x) { int i = y * FrameVerificationSource.Stride + x * 4; pixels[i + (x < FrameVerificationSource.Width / 2 ? 2 : 0)] = 255; pixels[i + 3] = 255; }
        workspace.Visibility = Visibility.Collapsed;
        sink.PushBgra(pixels, FrameVerificationSource.Stride, 1000);
        await UntilAsync(() => LocalView.PresentedFrameCount >= 1);
        workspace.Visibility = Visibility.Visible;
        await Task.Delay(100);
        await VerifyFramePixelsAsync(false); await SaveScreenshotAsync("render-bgra", LocalView);
        LocalView.IsMirrored = true;
        sink.PushBgra(pixels, FrameVerificationSource.Stride, 2000);
        await UntilAsync(() => LocalView.PresentedFrameCount >= 2); await Task.Delay(100); await VerifyFramePixelsAsync(true); await SaveScreenshotAsync("render-mirrored", LocalView);
        await Task.Run(() => { for (int i = 3; i <= 100; ++i) sink.PushBgra(pixels, FrameVerificationSource.Stride, i * 1000); });
        await UntilAsync(() => LocalView.LastPresentedTimestampMicroseconds == 100000);
        localPanel.Children.Remove(LocalView); await Task.Delay(50); localPanel.Children.Insert(0, LocalView);
        sink.PushBgra(pixels, FrameVerificationSource.Stride, 101000); await UntilAsync(() => LocalView.LastPresentedTimestampMicroseconds == 101000);
        LocalView.Track = null;

        // Use the result panel itself to check the single-view first-frame transition.
        RemoteView.FirstFrameFadeInDuration = 0.6;
        RemoteView.Track = stream.VideoTrack;
        Check(RemoteView.PresentedFrameCount == 0, "The result panel presented before receiving a frame.");
        sink.PushBgra(pixels, FrameVerificationSource.Stride, 102000);
        await UntilAsync(() => RemoteView.LastPresentedTimestampMicroseconds == 102000);
        if (new Windows.UI.ViewManagement.UISettings().AnimationsEnabled)
            await UntilAsync(() => RemoteView.Opacity is > 0 and < 1);
        await UntilAsync(() => RemoteView.Opacity == 1);
        RemoteView.Track = null;
    }
    private async Task VerifyFramePixelsAsync(bool mirrored)
    {
        var bitmap = new RenderTargetBitmap(); await bitmap.RenderAsync(LocalView, 160, 80);
        using var reader = DataReader.FromBuffer(await bitmap.GetPixelsAsync()); byte[] bytes = new byte[reader.UnconsumedBufferLength]; reader.ReadBytes(bytes);
        int left = (bitmap.PixelHeight / 2 * bitmap.PixelWidth + bitmap.PixelWidth / 8) * 4;
        int right = (bitmap.PixelHeight / 2 * bitmap.PixelWidth + bitmap.PixelWidth * 7 / 8) * 4;
        Check(bytes[left + (mirrored ? 0 : 2)] > 200 && bytes[right + (mirrored ? 2 : 0)] > 200, "WinUI displayed pixels or mirror metadata differ from the input.");
    }

    private sealed class FrameVerificationSource : IRealtimeExternalVideoSource
    {
        internal const int Width = 640;
        internal const int Height = 320;
        internal const int Stride = Width * 4;
        internal IRealtimeExternalFrameSink? Sink { get; private set; }
        public VideoFormat VideoFormat => new(Width, Height);

        public Task StartAsync(IRealtimeExternalFrameSink sink, CancellationToken cancellationToken)
        {
            Sink = sink;
            return Task.CompletedTask;
        }

        public Task StopAsync()
        {
            Sink = null;
            return Task.CompletedTask;
        }
    }
}
