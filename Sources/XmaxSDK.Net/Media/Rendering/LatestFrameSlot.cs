//
//  LatestFrameSlot.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK.Rendering;

internal sealed class LatestFrameSlot : IDisposable
{
    private readonly object gate = new();
    private XmaxVideoFrame? latest;
    private bool stopped;
    private ulong revision;
    internal void Publish(XmaxVideoFrame? frame, ulong contentRevision)
    {
        XmaxVideoFrame? old;
        lock (gate)
        {
            if (!stopped && frame is null && revision == contentRevision) return;
            old = stopped ? frame : latest;
            if (!stopped)
            {
                latest = frame;
                revision = contentRevision;
            }
        }
        old?.Dispose();
    }
    internal XmaxVideoFrame? Take(out ulong contentRevision)
    {
        lock (gate)
        {
            contentRevision = revision;
            var frame = latest;
            latest = null;
            return frame;
        }
    }
    public void Dispose()
    {
        XmaxVideoFrame? old;
        lock (gate) { stopped = true; old = latest; latest = null; }
        old?.Dispose();
    }
}
