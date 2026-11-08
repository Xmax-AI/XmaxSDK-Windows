//
//  MainViewModel.Beauty.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using XLab.Shared.Services;
using Xmax.SDK;

namespace XLab.Shared.ViewModels;

internal sealed partial class MainViewModel
{
    private BeautyPreferencesStore beautyStore = null!;
    private BeautyPreferences beauty = BeautyPreferences.Default;
    private readonly SemaphoreSlim beautyOperations = new(1, 1);
    private CancellationTokenSource? beautyDebounce;
    private Task beautyUpdate = Task.CompletedTask;
    private int beautyIntent;
    private bool beautyClosing;
    public bool BeautyEnabled { get => beauty.Enabled; set => UpdateBeauty(beauty with { Enabled = value }); }
    public bool BeautyPanelVisible => Active && BeautyEnabled;
    public bool BeautyCanEdit => !beautyClosing;
    public double BeautySmoothness { get => beauty.Options.Smoothness; set => SetStrength(value, options => options with { Smoothness = (float)value }); }
    public double BeautyLightening { get => beauty.Options.Lightening; set => SetStrength(value, options => options with { Lightening = (float)value }); }
    public double BeautySharpness { get => beauty.Options.Sharpness; set => SetStrength(value, options => options with { Sharpness = (float)value }); }
    public double BeautyRedness { get => beauty.Options.Redness; set => SetStrength(value, options => options with { Redness = (float)value }); }
    internal Task WaitForBeautyAsync() => beautyUpdate;
    private void InitializeBeauty(string? path) { beautyStore = new(path); beauty = beautyStore.Load(); }
    private void ResetBeautyForPreview() { ++beautyIntent; beautyDebounce?.Cancel(); beautyDebounce?.Dispose(); beautyDebounce = null; beauty = BeautyPreferences.Default; NotifyBeauty(); }
    private void SetStrength(double value, Func<RealtimeVideoBeautyOptions, RealtimeVideoBeautyOptions> change)
    {
        if (!BeautyEnabled || !double.IsFinite(value) || value < 0 || value > 1) return;
        UpdateBeauty(beauty with { Options = change(beauty.Options) });
    }
    private void UpdateBeauty(BeautyPreferences value)
    {
        if (beautyClosing || beauty == value) return;
        value.Options.Validate(); beauty = value;
        int intent = ++beautyIntent;
        beautyDebounce?.Cancel(); beautyDebounce?.Dispose(); beautyDebounce = new();
        beautyUpdate = DebounceBeautyAsync(session, revision, intent, beautyDebounce.Token);
        NotifyBeauty();
    }
    private async Task DebounceBeautyAsync(IXmaxRealtimeManaging? current, int epoch, int intent, CancellationToken token)
    {
        try {
            await Task.Delay(120, token);
            if (!IsPreview) beautyStore.Save(beauty);
            if (current is not null && !IsPreview) await ApplyBeautyAsync(current, epoch, intent, token);
        } catch (OperationCanceledException) { }
    }
    private async Task BindBeautyAsync(IXmaxRealtimeManaging current, int epoch)
    {
        await current.SetRemoteVideoBeautyErrorListenerAsync(_ => dispatcher.InvokeAsync(() => {
            if (current == session && epoch == revision && !beautyClosing) BeautyFailed();
        }));
        await ApplyBeautyAsync(current, epoch, beautyIntent, default);
    }
    private async Task ApplyBeautyAsync(IXmaxRealtimeManaging current, int epoch, int intent, CancellationToken token)
    {
        await beautyOperations.WaitAsync(token);
        try {
            if (current != session || epoch != revision || intent != beautyIntent || beautyClosing) return;
            try { await current.SetRemoteVideoBeautyAsync(beauty.Enabled ? beauty.Options : null); }
            catch (Exception error) {
                if (current != session || epoch != revision || intent != beautyIntent || beautyClosing ||
                    error is OperationCanceledException or ObjectDisposedException or XmaxException { Code: XmaxErrorCode.InvalidState or XmaxErrorCode.Cancelled }) return;
                try { await current.SetRemoteVideoBeautyAsync(null); } catch { }
                if (current == session && epoch == revision && intent == beautyIntent && !beautyClosing) BeautyFailed();
            }
        } finally { beautyOperations.Release(); }
    }
    private void BeautyFailed()
    {
        if (!BeautyEnabled || beautyClosing) return;
        UpdateBeauty(beauty with { Enabled = false });
        Error = "Beauty is unavailable and has been turned off. Generation will continue.";
    }
    private void BeginBeautyStop()
    {
        beautyClosing = true; ++beautyIntent;
        beautyDebounce?.Cancel(); beautyDebounce?.Dispose(); beautyDebounce = null;
        if (!IsPreview) beautyStore.Save(beauty);
        Changed(nameof(BeautyCanEdit));
    }
    private void EndBeautyStop() { beautyClosing = false; Changed(nameof(BeautyCanEdit)); }
    private void NotifyBeauty()
    {
        foreach (string name in new[] { nameof(BeautyEnabled), nameof(BeautyPanelVisible), nameof(BeautySmoothness), nameof(BeautyLightening), nameof(BeautySharpness), nameof(BeautyRedness) }) Changed(name);
    }
}
