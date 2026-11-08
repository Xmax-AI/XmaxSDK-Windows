//
//  RealtimeMediaStream.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>A local or remote stream identified by its controller and media binding.</summary>
public sealed class RealtimeMediaStream
{
    /// <summary>Gets the identifier of this media binding.</summary>
    public string Id { get; }

    /// <summary>Gets the video track to bind to an SDK video view.</summary>
    public RealtimeVideoTrack VideoTrack { get; }

    internal XmaxRealtime Owner { get; }
    internal ulong Revision { get; }
    internal bool IsLocal { get; }

    internal RealtimeMediaStream(XmaxRealtime owner, bool isLocal, ulong revision, bool cameraPreview = false)
    {
        Owner = owner;
        IsLocal = isLocal;
        Revision = revision;
        Id = $"{(isLocal ? "local" : "remote")}-{revision}";
        VideoTrack = new(owner, Id, !isLocal, revision, cameraPreview);
    }
}
