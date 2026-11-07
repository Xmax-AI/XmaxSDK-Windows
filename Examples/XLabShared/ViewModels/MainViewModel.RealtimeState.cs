//
//  MainViewModel.RealtimeState.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK;

namespace XLab.Shared.ViewModels;

internal sealed partial class MainViewModel
{
    private RealtimeConnectionState connectionState;
    private TaskCompletionSource? closingSession;

    public RealtimeConnectionState State
    {
        get => connectionState;
        private set
        {
            if (connectionState == value) return;
            connectionState = value;
            Changed();
            Changed(nameof(RemoteReady));
            Changed(nameof(ResultPlaceholder));
            RecordingChanged();
        }
    }

    public bool RemoteReady => State == RealtimeConnectionState.Generating;

    private void ReceiveState(RealtimeState snapshot)
    {
        if (snapshot.Reason is RealtimeReason.Failure failure)
            ReportError(failure.Error);

        State = snapshot.ConnectionState;
        if (State == RealtimeConnectionState.Idle)
        {
            localStream = null;
            LocalVideo = null;
        }
        if (State is RealtimeConnectionState.Idle or RealtimeConnectionState.Disconnecting)
            RemoteVideo = null;
        if (!Active && State is RealtimeConnectionState.Connected or RealtimeConnectionState.Generating)
        {
            Active = true;
            BeginElapsed();
        }
        Status = State switch
        {
            RealtimeConnectionState.Preparing => "正在准备摄像头…",
            RealtimeConnectionState.Ready => "摄像头已就绪 · 本地预览中",
            RealtimeConnectionState.Connecting => "正在连接服务…",
            RealtimeConnectionState.Connected => "正在等待首张结果帧…",
            RealtimeConnectionState.Generating => "实时生成中",
            _ => $"状态：{State}"
        };
        if (!RemoteReady && IsRecording) _ = FinishInterruptedRecordingAsync();
    }

    private async Task CloseRealtimeAsync(XmaxRealtime previous)
    {
        var idle = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        int closingRevision = revision;
        try
        {
            await previous.SetStateListenerAsync(snapshot =>
            {
                if (revision != closingRevision || idle.Task.IsCompleted) return;
                if (snapshot.ConnectionState is RealtimeConnectionState.Disconnecting or RealtimeConnectionState.Idle)
                    State = snapshot.ConnectionState;
                if (snapshot.ConnectionState == RealtimeConnectionState.Idle)
                    idle.TrySetResult();
            });
            await previous.CloseAsync();
            await idle.Task.WaitAsync(TimeSpan.FromSeconds(5));
        }
        finally { await previous.DisposeAsync(); }
    }
}
