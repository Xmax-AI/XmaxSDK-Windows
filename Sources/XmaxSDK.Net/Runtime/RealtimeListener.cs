//
//  RealtimeListener.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK.Runtime;

/// <summary>Delivers ordered or coalesced snapshots on the registering synchronization context.</summary>
internal sealed class RealtimeListener<T>(bool coalesce = false)
{
    private readonly object gate = new();
    private Registration? registration;
    private T latest = default!;
    private bool closed;

    internal void Set(Action<T>? listener, Action<Exception> report, bool replay = false,
        SynchronizationContext? callbackContext = null, TaskCompletionSource? replayCompletion = null)
    {
        lock (gate)
        {
            ObjectDisposedException.ThrowIf(closed, this);
            registration?.ReplayCompletion?.TrySetCanceled();
            registration = listener is null ? null : new(listener, callbackContext ?? SynchronizationContext.Current, report)
            { ReplayCompletion = replayCompletion };
            if (replay && registration is { } current)
                Enqueue(current, latest);
            else
                replayCompletion?.TrySetResult();
        }
    }

    internal void Close()
    {
        lock (gate)
        {
            closed = true;
            registration?.ReplayCompletion?.TrySetException(new ObjectDisposedException(nameof(RealtimeListener<T>)));
            registration = null;
        }
    }

    internal void Publish(T value)
    {
        lock (gate)
        {
            latest = value;
            if (registration is { } current)
                Enqueue(current, value);
        }
    }

    private void Enqueue(Registration current, T value)
    {
        if (coalesce)
            current.Pending.Clear();
        if (current.Pending.Count == 256)
            current.Pending.Dequeue();
        current.Pending.Enqueue(value);
        if (current.Scheduled)
            return;

        current.Scheduled = true;
        if (current.Context is { } context)
            context.Post(_ => Drain(current), null);
        else
            ThreadPool.QueueUserWorkItem(_ => Drain(current));
    }

    private void Drain(Registration current)
    {
        while (true)
        {
            Exception? failure = null;
            TaskCompletionSource? replayCompletion;
            lock (gate)
            {
                if (!ReferenceEquals(registration, current) || !current.Pending.TryDequeue(out var value))
                {
                    current.Pending.Clear();
                    current.Scheduled = false;
                    return;
                }
                replayCompletion = current.ReplayCompletion;
                current.ReplayCompletion = null;
                try { current.Listener(value); }
                catch (Exception error) { failure = error; }
            }
            if (failure is not null)
                current.Report(failure);
            replayCompletion?.TrySetResult();
        }
    }

    private sealed record Registration(Action<T> Listener, SynchronizationContext? Context, Action<Exception> Report)
    {
        internal Queue<T> Pending { get; } = new();
        internal bool Scheduled { get; set; }
        internal TaskCompletionSource? ReplayCompletion { get; set; }
    }
}
