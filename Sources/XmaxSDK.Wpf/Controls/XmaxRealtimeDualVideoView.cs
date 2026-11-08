//
//  XmaxRealtimeDualVideoView.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Xmax.SDK.Wpf;

/// <summary>Displays the generated result over a continuously running local preview.</summary>
/// <remarks>Bind each track to one video consumer. A new remote track remains hidden until its first frame is presented.</remarks>
public sealed class XmaxRealtimeDualVideoView : Grid
{
    /// <summary>Identifies the local track dependency property.</summary>
    public static readonly DependencyProperty LocalTrackProperty = DependencyProperty.Register(
        nameof(LocalTrack), typeof(RealtimeVideoTrack), typeof(XmaxRealtimeDualVideoView),
        new PropertyMetadata(null, (owner, args) => ((XmaxRealtimeDualVideoView)owner).localView.Track = (RealtimeVideoTrack?)args.NewValue));

    /// <summary>Identifies the remote track dependency property.</summary>
    public static readonly DependencyProperty RemoteTrackProperty = DependencyProperty.Register(
        nameof(RemoteTrack), typeof(RealtimeVideoTrack), typeof(XmaxRealtimeDualVideoView),
        new PropertyMetadata(null, (owner, args) => ((XmaxRealtimeDualVideoView)owner).remoteView.Track = (RealtimeVideoTrack?)args.NewValue));

    /// <summary>Identifies the shared video content mode dependency property.</summary>
    public static readonly DependencyProperty ContentModeProperty = DependencyProperty.Register(
        nameof(ContentMode), typeof(VideoContentMode), typeof(XmaxRealtimeDualVideoView),
        new PropertyMetadata(VideoContentMode.Fill, (owner, _) => ((XmaxRealtimeDualVideoView)owner).UpdateContentMode()));

    private readonly XmaxRealtimeVideoView localView = new() { IsHitTestVisible = false, FirstFrameFadeInDuration = 0 };
    private readonly XmaxRealtimeVideoView remoteView = new()
    {
        Opacity = 0,
        IsHitTestVisible = false,
        HideUntilFirstFrame = true
    };

    /// <summary>Gets or sets the local video track kept running beneath the result.</summary>
    public RealtimeVideoTrack? LocalTrack
    {
        get => (RealtimeVideoTrack?)GetValue(LocalTrackProperty);
        set => SetValue(LocalTrackProperty, value);
    }

    /// <summary>Gets or sets the generated track. Clearing it immediately reveals local preview.</summary>
    public RealtimeVideoTrack? RemoteTrack
    {
        get => (RealtimeVideoTrack?)GetValue(RemoteTrackProperty);
        set => SetValue(RemoteTrackProperty, value);
    }

    /// <summary>Gets or sets the shared scaling and cropping mode. The default is Fill.</summary>
    public VideoContentMode ContentMode
    {
        get => (VideoContentMode)GetValue(ContentModeProperty);
        set => SetValue(ContentModeProperty, value);
    }

    /// <summary>Gets or sets the remote first-frame fade duration, in seconds. A nonpositive or nonfinite value disables it.</summary>
    public double RemoteVideoFadeInDuration
    {
        get => remoteView.FirstFrameFadeInDuration;
        set => remoteView.FirstFrameFadeInDuration = value;
    }

    /// <summary>Gets or sets whether pointer trajectories are accepted on the visible result.</summary>
    public bool IsInteractionEnabled
    {
        get => remoteView.IsInteractionEnabled;
        set => remoteView.IsInteractionEnabled = value;
    }

    /// <summary>Gets or sets whether the result is mirrored in addition to honoring track mirror metadata.</summary>
    public bool IsMirrored
    {
        get => remoteView.IsMirrored;
        set => remoteView.IsMirrored = value;
    }

    /// <summary>Gets or sets the result trajectory renderer. Null restores the SDK effect.</summary>
    public IXmaxTrajectoryEffectRenderer? TrajectoryRenderer
    {
        get => remoteView.TrajectoryRenderer;
        set => remoteView.TrajectoryRenderer = value;
    }

    /// <summary>Occurs on the UI thread when either video track cannot be presented.</summary>
    public event Action<Exception>? PlaybackFailed;

    /// <summary>Creates the local preview and result layers in stacking order.</summary>
    public XmaxRealtimeDualVideoView()
    {
        Background = Brushes.Black;
        ClipToBounds = true;
        Children.Add(localView);
        Children.Add(remoteView);
        localView.PlaybackFailed += failure => PlaybackFailed?.Invoke(failure);
        remoteView.PlaybackFailed += failure => PlaybackFailed?.Invoke(failure);
    }

    private void UpdateContentMode() => localView.ContentMode = remoteView.ContentMode = ContentMode;
}
