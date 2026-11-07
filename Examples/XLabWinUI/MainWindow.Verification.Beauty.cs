//
//  MainWindow.Verification.Beauty.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    private async Task VerifyBeautyAsync()
    {
        static void Check(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
        Check(Model.IsPreview && Model.Active && Model.Session is null, "Beauty UI check must not start capture or generation.");
        ResizeWindow(1180, 800); await Task.Delay(180); ArrangePage(); root.UpdateLayout();
        Check(beautyButton.Text == "Beauty" && !beautyButton.ExtraChevron && !beautyButton.IsLoading, "Beauty label must stay fixed without a chevron or loading suffix.");
        Check(beautyPanel.Visibility == Visibility.Visible && beautyPanel.Parent == remotePanel, "Beauty controls must appear only in the result panel.");
        var origin = beautyPanel.TransformToVisual(remotePanel).TransformPoint(new(0, 0));
        Check(Math.Abs(remotePanel.ActualWidth - origin.X - beautyPanel.ActualWidth - 12) < 2 && Math.Abs(remotePanel.ActualHeight - origin.Y - beautyPanel.ActualHeight - 12) < 2, "Beauty panel must align 12 units from the result bottom-right.");
        for (int i = 0; i < 4; ++i) {
            var slider = beautyPanel.Sliders[i];
            Check(slider.Value == 0 && slider.ActualWidth > 0 && slider.ActualHeight > 0, "Beauty defaults and slider layout must match Qt.");
            var peer = FrameworkElementAutomationPeer.CreatePeerForElement(slider)!;
            ((IRangeValueProvider)peer.GetPattern(PatternInterface.RangeValue)).SetValue((i + 1) * .2);
        }
        await Task.Delay(40);
        Check(Math.Abs(Model.BeautySmoothness - .2) < .005 && Math.Abs(Model.BeautyLightening - .4) < .005 && Math.Abs(Model.BeautySharpness - .6) < .005 && Math.Abs(Model.BeautyRedness - .8) < .005, "All four sliders must update shared SDK parameters.");
        var invoke = (IInvokeProvider)FrameworkElementAutomationPeer.CreatePeerForElement(beautyButton)!.GetPattern(PatternInterface.Invoke);
        invoke.Invoke(); await Task.Delay(40);
        Check(!Model.BeautyEnabled && beautyPanel.Visibility == Visibility.Collapsed, "Disabling beauty must hide its panel.");
        invoke.Invoke(); await Task.Delay(40);
        Check(Model.BeautyEnabled && beautyPanel.Visibility == Visibility.Visible && Math.Abs(Model.BeautySmoothness - .2) < .005, "Reopening beauty must retain strengths.");
        Model.BeautySmoothness = double.NaN;
        Check(Math.Abs(Model.BeautySmoothness - .2) < .005, "Invalid strengths must be rejected.");
        await Model.StopAsync();
        Check(Model.Session is null && beautyPanel.Visibility == Visibility.Collapsed, "Stopping must hide beauty without opening a session.");
    }
}
