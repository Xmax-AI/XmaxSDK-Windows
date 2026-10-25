namespace Xmax.SDK;

/// <summary>A single-consumer latest-frame mailbox. The caller owns and must dispose acquired frames.</summary>
public interface IXmaxVideoSource
{
    XmaxVideoFrame? TryAcquireFrame();
}
