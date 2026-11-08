//
//  ExternalSourceDispatch.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Runs external source lifecycle work on its controller's UI context when available.</summary>
internal static class ExternalSourceDispatch
{
    internal static Task InvokeAsync(SynchronizationContext? context, Func<Task> action) =>
        InvokeAsync(context, async () =>
        {
            await action();
            return true;
        });

    internal static Task<T> InvokeAsync<T>(SynchronizationContext? context, Func<Task<T>> action)
    {
        if (context is null || context == SynchronizationContext.Current)
            return action();

        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        context.Post(async _ =>
        {
            try { completion.SetResult(await action()); }
            catch (OperationCanceledException error) { completion.SetCanceled(error.CancellationToken); }
            catch (Exception error) { completion.SetException(error); }
        }, null);
        return completion.Task;
    }
}
