//
//  MainWindow.Verification.Beauty.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Automation.Provider;

namespace XLabWpf.Views;

public partial class MainWindow
{
    internal async Task VerifyBeautyAsync()
    {
        static void Check(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
        Check(Model.IsPreview && Model.Active && Model.Session is null, "Beauty UI check must not start capture or generation.");
        Width = 1180; Height = 800; await Task.Delay(180); ArrangePage(); UpdateLayout();
        Check(BeautyButton.Text == "Beauty" && !BeautyButton.TrailingGlyph && BeautyButton.Glyph == "sparkles", "Beauty label must stay fixed without a chevron or loading suffix.");
        Check(BeautyControls.IsVisible && BeautyControls.Parent == RemotePanel, "Beauty controls must appear only in the result panel.");
        var origin = BeautyControls.TranslatePoint(new(0, 0), RemotePanel);
        Check(Math.Abs(RemotePanel.ActualWidth - origin.X - BeautyControls.ActualWidth - 12) < 2 && Math.Abs(RemotePanel.ActualHeight - origin.Y - BeautyControls.ActualHeight - 12) < 2, "Beauty panel must align 12 units from the result bottom-right.");
        for (int i = 0; i < 4; ++i) {
            var slider = BeautyControls.Sliders[i];
            Check(slider.Value == 0 && slider.ActualWidth > 0 && slider.ActualHeight > 0, "Beauty defaults and slider layout must match Qt.");
            var peer = UIElementAutomationPeer.CreatePeerForElement(slider)!;
            ((IRangeValueProvider)peer.GetPattern(PatternInterface.RangeValue)).SetValue((i + 1) * .2);
        }
        await Task.Delay(40);
        Check(Math.Abs(Model.BeautySmoothness - .2) < .005 && Math.Abs(Model.BeautyLightening - .4) < .005 && Math.Abs(Model.BeautySharpness - .6) < .005 && Math.Abs(Model.BeautyRedness - .8) < .005, "All four sliders must update shared SDK parameters.");
        var invoke = (IInvokeProvider)UIElementAutomationPeer.CreatePeerForElement(BeautyButton)!.GetPattern(PatternInterface.Invoke);
        invoke.Invoke(); await Task.Delay(40);
        Check(!Model.BeautyEnabled && !BeautyControls.IsVisible, "Disabling beauty must hide its panel.");
        invoke.Invoke(); await Task.Delay(40);
        Check(Model.BeautyEnabled && BeautyControls.IsVisible && Math.Abs(Model.BeautySmoothness - .2) < .005, "Reopening beauty must retain strengths.");
        Model.BeautySmoothness = double.NaN;
        Check(Math.Abs(Model.BeautySmoothness - .2) < .005, "Invalid strengths must be rejected.");
        await Model.StopAsync();
        Check(Model.Session is null && !BeautyControls.IsVisible, "Stopping must hide beauty without opening a session.");
    }
}
