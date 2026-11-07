//
//  VideoFramePump.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK.Rendering;

internal sealed class VideoFramePump : IAsyncDisposable
{
    private readonly CancellationTokenSource stop = new();
    private readonly Task worker;
    internal LatestFrameSlot Frames { get; } = new();
    internal VideoFramePump(IXmaxVideoSource source, Action<Exception> failure)
    {
        worker = Task.Run(async () =>
        {
            try
            {
                while (!stop.IsCancellationRequested)
                {
                    ulong revision = 0;
                    var frame = source is RealtimeVideoTrack track
                        ? track.TryAcquireFrame(out revision)
                        : source.TryAcquireFrame();
                    Frames.Publish(frame, revision);
                    await Task.Delay(8, stop.Token).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException) when (stop.IsCancellationRequested) { }
            catch (ObjectDisposedException) { }
            catch (Exception error) { failure(error); }
        });
    }
    public async ValueTask DisposeAsync()
    {
        stop.Cancel();
        Frames.Dispose();
        await worker.ConfigureAwait(false);
        stop.Dispose();
    }
}
