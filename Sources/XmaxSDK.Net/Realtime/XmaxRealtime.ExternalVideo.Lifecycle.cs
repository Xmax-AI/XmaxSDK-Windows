//
//  XmaxRealtime.ExternalVideo.Lifecycle.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

public sealed partial class XmaxRealtime
{
    private readonly SemaphoreSlim localMediaGate = new(1);
    private readonly object externalStartupGate = new();
    private CancellationTokenSource? externalStartup;
    private Task? externalStartupCancellation;
    private int externalCloseCount;
    private bool disposingLocalMedia;
    private ExternalAttachment? externalAttachment;

    private async Task<T> ChangeLocalMediaAsync<T>(
        Func<Task<T>> change,
        CancellationToken cancellationToken = default
    )
    {
        await localMediaGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            lock (externalStartupGate)
            {
                ObjectDisposedException.ThrowIf(disposingLocalMedia, this);
                if (externalCloseCount != 0)
                    throw new OperationCanceledException("The controller is closing local media.");
            }
            try { return await change().ConfigureAwait(false); }
            finally { await DetachInactiveExternalSourceAsync().ConfigureAwait(false); }
        }
        finally
        {
            localMediaGate.Release();
        }
    }

    private Task ChangeLocalMediaAsync(Func<Task> change) => ChangeLocalMediaAsync(async () =>
    {
        await change().ConfigureAwait(false);
        return true;
    });

    private void BeginExternalClose(bool disposing = false)
    {
        lock (externalStartupGate)
        {
            disposingLocalMedia |= disposing;
            ++externalCloseCount;
            _ = CancelExternalStartup();
        }
    }

    private Task CancelExternalStartup() =>
        externalStartup is null
            ? Task.CompletedTask
            : externalStartupCancellation ??= externalStartup.CancelAsync();

    private async Task ObserveExternalCancellationAsync(Task cancellation)
    {
        try { await cancellation.ConfigureAwait(false); }
        catch (Exception error) { NotifyCallbackFault(error); }
    }

    private async Task FinishExternalCloseAsync(bool checkLateAdmission = false)
    {
        await localMediaGate.WaitAsync().ConfigureAwait(false);
        try
        {
            try
            {
                if (checkLateAdmission)
                    await Run(value =>
                    {
                        if (GetState(value).ConnectionState != RealtimeConnectionState.Idle)
                            CloseNative(value);
                    }).ConfigureAwait(false);
            }
            catch (ObjectDisposedException) { }
            finally { await DetachExternalSourceAsync().ConfigureAwait(false); }
        }
        finally { localMediaGate.Release(); }
    }

    private async Task ReconcileExternalSourceAsync()
    {
        await localMediaGate.WaitAsync().ConfigureAwait(false);
        try { await DetachInactiveExternalSourceAsync().ConfigureAwait(false); }
        catch (Exception error) { NotifyCallbackFault(error); }
        finally { localMediaGate.Release(); }
    }

    private async Task DetachInactiveExternalSourceAsync()
    {
        if (externalAttachment is { } attachment && !attachment.Sink.IsActive)
            await DetachExternalSourceAsync().ConfigureAwait(false);
    }

    private async Task DetachExternalSourceAsync()
    {
        var attachment = externalAttachment;
        Volatile.Write(ref externalAttachment, null);
        if (attachment is null)
            return;
        try { await attachment.Source.StopAsync().ConfigureAwait(false); }
        catch (Exception error) { NotifyCallbackFault(error); }
        finally { attachment.Sink.Dispose(); }
    }

    private sealed record ExternalAttachment(IRealtimeExternalVideoSource Source, ExternalFrameSink Sink);
}
