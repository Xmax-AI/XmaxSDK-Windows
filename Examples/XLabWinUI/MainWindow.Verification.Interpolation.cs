//
//  MainWindow.Verification.Interpolation.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Reflection;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using XLab.Shared.ViewModels;
using Xmax.SDK;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    private async Task VerifyInterpolationAsync()
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtimeManager();
        var field = typeof(MainViewModel).GetField("session", BindingFlags.Instance | BindingFlags.NonPublic)!;
        string? previousDevice = Environment.GetEnvironmentVariable("XMAX_INTERPOLATION_DEVICE");
        field.SetValue(Model, realtime);
        try
        {
            Environment.SetEnvironmentVariable("XMAX_INTERPOLATION_DEVICE", "-1");
            RefreshUi();
            Check(Model.Active && interpolationButton.IsEnabled && !Model.InterpolationEnabled,
                "The interpolation toggle must start off and be available during a session.");
            var invoke = (IInvokeProvider)FrameworkElementAutomationPeer.CreatePeerForElement(interpolationButton)!.GetPattern(PatternInterface.Invoke);
            invoke.Invoke();
            await UntilAsync(() => !Model.InterpolationSwitching && (Model.InterpolationEnabled || Model.Error.Length > 0));
            Check(Model.InterpolationEnabled && interpolationButton.Accent && Model.Error.Length == 0,
                "The interpolation toggle failed to initialize the real RIFE model: " + Model.Error);
            Check((await realtime.GetRemoteVideoInterpolationStatusAsync()).Enabled,
                "The enabled UI state did not reach the native SDK.");
            await SaveScreenshotAsync("interpolation-enabled");
            invoke.Invoke();
            await UntilAsync(() => !Model.InterpolationSwitching && !Model.InterpolationEnabled);
            Check(!Model.InterpolationEnabled && !(await realtime.GetRemoteVideoInterpolationStatusAsync()).Enabled,
                "The interpolation toggle failed to release its processor.");
            Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Idle,
                "Configuring interpolation started a camera or cloud session.");
        }
        finally
        {
            field.SetValue(Model, null);
            Environment.SetEnvironmentVariable("XMAX_INTERPOLATION_DEVICE", previousDevice);
            RefreshUi();
        }
    }
}
