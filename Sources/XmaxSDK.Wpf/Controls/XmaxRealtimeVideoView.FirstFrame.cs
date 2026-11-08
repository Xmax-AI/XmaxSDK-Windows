//
//  XmaxRealtimeVideoView.FirstFrame.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Windows;
using System.Windows.Media.Animation;

namespace Xmax.SDK.Wpf;

public sealed partial class XmaxRealtimeVideoView
{
    private bool firstFrameFadeApplied;

    /// <summary>Gets or sets the first-frame fade duration in seconds. The default is 0.3 seconds.</summary>
    /// <remarks>Reduced motion or a nonpositive or nonfinite value disables animation. Changes apply to the next first frame.</remarks>
    public double FirstFrameFadeInDuration { get; set; } = 0.3;

    internal bool HideUntilFirstFrame { get; init; }

    private void ResetFirstFramePresentation()
    {
        if (firstFrameFadeApplied)
        {
            BeginAnimation(OpacityProperty, null);
            firstFrameFadeApplied = false;
        }
        if (!HideUntilFirstFrame) return;
        Opacity = 0;
        IsHitTestVisible = false;
    }

    private async Task ConfirmPreviewAsync(RealtimeVideoTrack track)
    {
        try { await track.NotifyPreviewAttachedAsync(); }
        catch (ObjectDisposedException) { }
        catch (Exception failure)
        {
            if (IsLoaded && ReferenceEquals(Track, track)) PlaybackFailed?.Invoke(failure);
        }
    }

    private void PresentFirstFrame()
    {
        if (HideUntilFirstFrame)
        {
            Opacity = 1;
            IsHitTestVisible = true;
        }

        double seconds = FirstFrameFadeInDuration;
        if (!double.IsFinite(seconds) || seconds <= 0 || seconds >= TimeSpan.MaxValue.TotalSeconds ||
            !SystemParameters.ClientAreaAnimation)
            return;
        BeginAnimation(OpacityProperty, new DoubleAnimation(0, Opacity, new Duration(TimeSpan.FromSeconds(seconds)))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseInOut },
            FillBehavior = FillBehavior.Stop
        });
        firstFrameFadeApplied = true;
    }
}
