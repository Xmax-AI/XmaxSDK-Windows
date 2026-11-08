//
//  XmaxRealtime.Listeners.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Runtime;

namespace Xmax.SDK;

public sealed partial class XmaxRealtime
{
    private readonly RealtimeListener<RealtimeState> stateListener = new();
    private readonly RealtimeListener<RealtimeLaunchTiming> timingListener = new();
    private readonly RealtimeListener<UplinkVideoStatistics?> uplinkStatisticsListener = new(coalesce: true);
    private readonly RealtimeListener<DownlinkVideoStatistics?> downlinkStatisticsListener = new(coalesce: true);
    private readonly RealtimeListener<RealtimeNetworkQuality> networkListener = new(coalesce: true);
    private readonly RealtimeListener<RealtimePerformanceAlarm> performanceListener = new();
    private readonly RealtimeListener<float?> microphoneListener = new(coalesce: true, isReset: level => level is null);
    private readonly object stateRegistrationGate = new();
    private readonly Queue<StateRegistration> stateRegistrations = new();
    private bool stateRegistrationsClosed;

    /// <summary>Registers a state listener and replays the latest state, or clears it when null.</summary>
    /// <remarks>
    /// Register from the UI thread, or supply its synchronization context, for UI-thread callbacks.
    /// Headless callers without a synchronization context receive serialized worker-thread callbacks.
    /// A registration completes after its initial callback returns. Replacing it before replay cancels its task.
    /// </remarks>
    public Task SetStateListenerAsync(Action<RealtimeState>? listener, SynchronizationContext? callbackContext = null)
    {
        var ready = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (stateRegistrationGate)
        {
            ObjectDisposedException.ThrowIf(stateRegistrationsClosed, this);
            stateRegistrations.Enqueue(new(listener, callbackContext ?? SynchronizationContext.Current, ready));
        }
        return ready.Task;
    }

    private void ApplyStateRegistrations()
    {
        StateRegistration[] pending;
        lock (stateRegistrationGate)
        {
            pending = [.. stateRegistrations];
            stateRegistrations.Clear();
        }
        foreach (var item in pending)
        {
            try
            {
                stateListener.Set(item.Listener, NotifyCallbackFault, replay: true, callbackContext: item.Context,
                    replayCompletion: item.Ready);
            }
            catch (Exception error) { item.Ready.TrySetException(error); }
        }
    }

    private void CloseStateRegistrations()
    {
        lock (stateRegistrationGate)
        {
            stateRegistrationsClosed = true;
            while (stateRegistrations.TryDequeue(out var item))
                item.Ready.TrySetException(new ObjectDisposedException(nameof(XmaxRealtime)));
        }
    }

    private sealed record StateRegistration(
        Action<RealtimeState>? Listener, SynchronizationContext? Context, TaskCompletionSource Ready);

    /// <summary>Registers a generation startup timing listener, or clears it when null.</summary>
    public Task SetLaunchTimingListenerAsync(Action<RealtimeLaunchTiming>? listener)
    {
        timingListener.Set(listener, NotifyCallbackFault);
        return Task.CompletedTask;
    }

    /// <summary>Registers the uplink video statistics listener, including unavailable snapshots.</summary>
    public Task SetUplinkVideoStatisticsListenerAsync(Action<UplinkVideoStatistics?>? listener)
    {
        uplinkStatisticsListener.Set(listener, NotifyCallbackFault, replay: true);
        return Task.CompletedTask;
    }

    /// <summary>Registers the downlink video statistics listener, including unavailable snapshots.</summary>
    public Task SetDownlinkVideoStatisticsListenerAsync(Action<DownlinkVideoStatistics?>? listener)
    {
        downlinkStatisticsListener.Set(listener, NotifyCallbackFault, replay: true);
        return Task.CompletedTask;
    }

    /// <summary>Registers a directional network quality listener, or clears it when null.</summary>
    public Task SetNetworkQualityListenerAsync(Action<RealtimeNetworkQuality>? listener)
    {
        networkListener.Set(listener, NotifyCallbackFault);
        return Task.CompletedTask;
    }

    /// <summary>Registers a device performance alarm listener, or clears it when null.</summary>
    public Task SetPerformanceAlarmListenerAsync(Action<RealtimePerformanceAlarm>? listener)
    {
        performanceListener.Set(listener, NotifyCallbackFault);
        return Task.CompletedTask;
    }

    /// <summary>Observes physical microphone levels from 0 to 1, or null when unavailable.</summary>
    /// <remarks>Registration awaits the initial reading. A capture reset is delivered before newer samples.</remarks>
    public Task SetMicrophoneLevelListenerAsync(Action<float?>? listener)
    {
        var ready = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        microphoneListener.Set(listener, NotifyCallbackFault, replay: true, replayCompletion: ready);
        return ready.Task;
    }

    /// <summary>Registers serialized remote audio and processed video delivery.</summary>
    public Task SetRemoteMediaFrameListenerAsync(Action<XmaxMediaFrame>? listener) =>
        SetRemoteMediaListenerAsync(listener);

    private void DispatchTypedEvent(RealtimeEvent item)
    {
        switch (item.Type)
        {
            case RealtimeEventType.State:
                stateListener.Publish(RealtimeState.From(item));
                if (item.State is RealtimeConnectionState.Idle or RealtimeConnectionState.Ready or RealtimeConnectionState.Disconnecting)
                {
                    uplinkStatisticsListener.Publish(null);
                    downlinkStatisticsListener.Publish(null);
                    networkListener.Publish(new(
                        RealtimeNetworkQualityLevel.Unknown,
                        RealtimeNetworkQualityLevel.Unknown
                    ));
                }
                break;
            case RealtimeEventType.Timing:
                timingListener.Publish(new(
                    AvailableMilliseconds(item.SessionMilliseconds),
                    AvailableMilliseconds(item.JoinMilliseconds),
                    AvailableMilliseconds(item.PublishMilliseconds),
                    AvailableMilliseconds(item.FirstFrameMilliseconds),
                    AvailableMilliseconds(item.TotalMilliseconds)
                ));
                break;
            case RealtimeEventType.NetworkQuality:
                networkListener.Publish(new(
                    DecodeNetworkQuality(item.Quality),
                    DecodeNetworkQuality(item.Code)
                ));
                break;
            case RealtimeEventType.Quality when item.Direction == 1 && item.Code == 0:
                using (var lease = handle.Borrow()) RefreshResultTrackFormat(lease.Value);
                break;
            case RealtimeEventType.Statistics when item.Quality < 0 && item.Direction == 0:
                uplinkStatisticsListener.Publish(GetUplinkVideoStatistics());
                break;
            case RealtimeEventType.Statistics when item.Quality < 0 && item.Direction == 1:
            {
                var snapshot = GetDownlinkVideoStatistics();
                var resultTrack = Volatile.Read(ref currentRemoteStream)?.VideoTrack;
                if (resultTrack is not null && snapshot is { Width: int width, Height: int height } && width > 0 && height > 0)
                {
                    uint? fps = snapshot.DecodeFrameRate is > 0 ? (uint)snapshot.DecodeFrameRate.Value : null;
                    resultTrack.ObserveFrame((uint)width, (uint)height, fps);
                }
                else if (snapshot is null)
                    resultTrack?.ClearObservedFormat();
                downlinkStatisticsListener.Publish(snapshot);
                break;
            }
            case RealtimeEventType.Performance:
                performanceListener.Publish(new(item.Code != 0,
                    item.Width > 0 && item.Height > 0 && item.FramesPerSecond > 0
                        ? new VideoFormat((uint)item.Width, (uint)item.Height, (uint)item.FramesPerSecond)
                        : null));
                break;
            case RealtimeEventType.MicrophoneLevel:
                microphoneListener.Publish(item.Code < 0 ? null : Math.Clamp(item.Code, 0, 255) / 255f);
                break;
        }
    }

    private void ClearTypedListeners()
    {
        stateListener.Close();
        timingListener.Close();
        uplinkStatisticsListener.Close();
        downlinkStatisticsListener.Close();
        networkListener.Close();
        performanceListener.Close();
        microphoneListener.Close();
    }

    private static RealtimeNetworkQualityLevel DecodeNetworkQuality(int value) =>
        value is >= 0 and <= 6 ? (RealtimeNetworkQualityLevel)value : RealtimeNetworkQualityLevel.Unknown;

    private static double? AvailableMilliseconds(double value) => value >= 0 ? value : null;
}
