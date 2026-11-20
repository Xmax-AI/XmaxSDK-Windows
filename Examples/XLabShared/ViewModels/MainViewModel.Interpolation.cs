//
//  MainViewModel.Interpolation.cs
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
    private bool interpolationEnabled;
    private bool interpolationSwitching;
    private bool interpolationPolling;
    public bool InterpolationEnabled => interpolationEnabled;
    public bool InterpolationSwitching => interpolationSwitching;
    public bool InterpolationCanEdit => session is not null && Active && !Busy && !interpolationSwitching;

    public async Task ToggleInterpolationAsync()
    {
        var current = session;
        if (!InterpolationCanEdit || current is null) return;
        Error = "";
        interpolationSwitching = true;
        NotifyInterpolation();
        try
        {
            bool enabled = !interpolationEnabled;
            int device = int.TryParse(Environment.GetEnvironmentVariable("XMAX_INTERPOLATION_DEVICE"), out int selected) ? selected : 0;
            await current.SetRemoteVideoInterpolationAsync(enabled ? new(
                ModelDirectory: Environment.GetEnvironmentVariable("XMAX_INTERPOLATION_MODEL_DIR"), DeviceIndex: device) : null);
            if (current == session) interpolationEnabled = enabled;
        }
        catch (Exception failure) { if (current == session) ReportError(failure); }
        finally { interpolationSwitching = false; NotifyInterpolation(); }
    }

    private async Task PollInterpolationAsync()
    {
        var current = session;
        if (!interpolationEnabled || interpolationSwitching || interpolationPolling || current is null) return;
        interpolationPolling = true;
        try
        {
            var status = await current.GetRemoteVideoInterpolationStatusAsync();
            if (current != session) return;
            interpolationEnabled = status.Enabled;
            if (status.Error is not null) ReportError(status.Error);
            NotifyInterpolation();
        }
        catch (Exception failure) when (current != session || failure is ObjectDisposedException) { }
        catch (Exception failure) { ReportError(failure); }
        finally { interpolationPolling = false; }
    }

    private void ResetInterpolation()
    {
        interpolationEnabled = false;
        NotifyInterpolation();
    }

    private void NotifyInterpolation()
    {
        Changed(nameof(InterpolationEnabled));
        Changed(nameof(InterpolationSwitching));
        Changed(nameof(InterpolationCanEdit));
    }
}
