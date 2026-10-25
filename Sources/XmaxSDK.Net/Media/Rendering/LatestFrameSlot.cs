namespace Xmax.SDK.Rendering;

internal sealed class LatestFrameSlot : IDisposable
{
    private readonly object gate = new();
    private XmaxVideoFrame? latest;
    private bool stopped;
    internal void Replace(XmaxVideoFrame frame)
    {
        XmaxVideoFrame? old;
        lock (gate)
        {
            old = stopped ? frame : latest;
            if (!stopped) latest = frame;
        }
        old?.Dispose();
    }
    internal XmaxVideoFrame? Take()
    { lock (gate) { var frame = latest; latest = null; return frame; } }
    public void Dispose()
    {
        XmaxVideoFrame? old;
        lock (gate) { stopped = true; old = latest; latest = null; }
        old?.Dispose();
    }
}
