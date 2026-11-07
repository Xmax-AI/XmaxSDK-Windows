//
//  XmaxRealtimeVideoView.FirstFrame.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media.Animation;

namespace Xmax.SDK.WinUI;

public sealed partial class XmaxRealtimeVideoView
{
    private Storyboard? firstFrameFade;

    /// <summary>Gets or sets the first-frame fade duration in seconds. The default is 0.3 seconds.</summary>
    /// <remarks>Reduced motion or a nonpositive or nonfinite value disables animation. Changes apply to the next first frame.</remarks>
    public double FirstFrameFadeInDuration { get; set; } = 0.3;

    internal bool HideUntilFirstFrame { get; init; }

    private void ResetFirstFramePresentation()
    {
        firstFrameFade?.Stop();
        firstFrameFade = null;
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
            if (IsLoaded && ReferenceEquals(VideoSource, track)) PlaybackFailed?.Invoke(failure);
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
            !new Windows.UI.ViewManagement.UISettings().AnimationsEnabled)
            return;
        var animation = new DoubleAnimation
        {
            From = 0,
            To = Opacity,
            Duration = new Duration(TimeSpan.FromSeconds(seconds)),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseInOut },
            FillBehavior = FillBehavior.Stop
        };
        Storyboard.SetTarget(animation, this);
        Storyboard.SetTargetProperty(animation, "Opacity");
        firstFrameFade = new Storyboard();
        firstFrameFade.Children.Add(animation);
        firstFrameFade.Begin();
    }
}
