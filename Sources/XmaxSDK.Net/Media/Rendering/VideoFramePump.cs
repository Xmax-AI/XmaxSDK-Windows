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
                    var frame = source.TryAcquireFrame();
                    if (frame is not null) Frames.Replace(frame);
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
