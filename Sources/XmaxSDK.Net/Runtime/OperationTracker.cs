using Xmax.SDK.Interop;

namespace Xmax.SDK.Runtime;

/// <summary>Tracks lifetime without serializing native admission, replacement, or cancellation.</summary>
internal sealed class OperationTracker(NativeHandle handle)
{
    private readonly object gate = new();
    private readonly HashSet<Task> pending = [];
    private bool closing;
    private Task? disposal;

    internal Task<T> Run<T>(Func<nint, T> operation, CancellationToken token = default)
    {
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(closing, this);
            token.ThrowIfCancellationRequested();
            var lease = handle.Borrow();
            Task<T> task;
            try
            {
                task = Task.Run(() =>
                {
                    using (lease)
                    {
                        lock (gate) ObjectDisposedException.ThrowIf(closing, this);
                        token.ThrowIfCancellationRequested();
                        return operation(lease.Value);
                    }
                });
            }
            catch { lease.Dispose(); throw; }
            pending.Add(task);
            _ = task.ContinueWith(t => { lock (gate) pending.Remove(t); }, CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
            return task;
        }
    }

    internal Task DisposeAsync(Action<nint> close, Func<Task>? drain = null)
    {
        lock (gate)
        {
            if (disposal is not null) return disposal;
            closing = true;
            Task[] active = [.. pending];
            // A separate worker is essential: close cancels blocking startup rather than waiting behind it.
            disposal = Task.Run(async () =>
            {
                Exception? failure = null;
                Task completion = Task.WhenAll(active);
                do
                {
                    try { using var lease = handle.Borrow(); close(lease.Value); }
                    catch (Exception error) { failure ??= error; }
                    if (!completion.IsCompleted)
                        await Task.WhenAny(completion, Task.Delay(10)).ConfigureAwait(false);
                } while (!completion.IsCompleted);
                try { await completion.ConfigureAwait(false); } catch { /* returned to their callers */ }
                try { if (drain is not null) await drain().ConfigureAwait(false); }
                finally { handle.Dispose(); }
                if (failure is not null) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
            });
            return disposal;
        }
    }
}
