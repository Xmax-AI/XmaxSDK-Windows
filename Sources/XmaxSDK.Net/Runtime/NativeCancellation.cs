namespace Xmax.SDK.Runtime;

internal static class NativeCancellation
{
    internal static T Run<T>(CancellationToken token, Func<T> operation, Action cancel)
    {
        token.ThrowIfCancellationRequested();
        object gate = new();
        bool active = true;
        Task? cancellation = null;
        using var registration = token.Register(() =>
        {
            lock (gate)
                if (active) cancellation = Task.Run(() =>
                {
                    // Cancellation may win the race before native admission. Keep requesting it until
                    // the operation exits, otherwise a late native start could survive an earlier close.
                    for (;;)
                    {
                        lock (gate) if (!active) return;
                        cancel();
                        lock (gate) if (!active) return;
                        Thread.Sleep(10);
                    }
                });
        });
        try
        {
            token.ThrowIfCancellationRequested();
            T result = operation();
            token.ThrowIfCancellationRequested();
            return result;
        }
        catch (Exception error) when (token.IsCancellationRequested)
        { throw new OperationCanceledException("Native operation cancelled after cleanup.", error, token); }
        finally
        {
            Task? pending;
            lock (gate) { active = false; pending = cancellation; }
            // Keep the caller's handle lease alive until cancellation has actually finished.
            pending?.GetAwaiter().GetResult();
        }
    }
}
