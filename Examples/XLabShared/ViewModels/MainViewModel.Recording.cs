using XLab.Shared.Recording;
using Xmax.SDK;

namespace XLab.Shared.ViewModels;

internal sealed partial class MainViewModel
{
    private RemoteMediaRecorder? recorder, completedRecording;
    private XmaxRealtime? recordingSession;
    private Task? recordingRegistration, recordingStop;
    private bool savingRecording;
    private string? recordingPath;

    public bool IsRecording => recorder is not null;
    public bool IsFinishingRecording => recordingStop is not null;
    public bool IsSavingRecording => savingRecording;
    public bool HasRecordingToSave => recordingPath is not null;
    public bool CanRecord => Active && RemoteReady && !IsPreview && !Busy && session is not null &&
        !IsRecording && !IsFinishingRecording && !HasRecordingToSave && !IsSavingRecording;
    public string RecordLabel => IsFinishingRecording ? "Finishing…" : HasRecordingToSave || IsSavingRecording ? "Saving…" : IsRecording ? "Stop Recording" : "Record";

    private void RecordingChanged()
    {
        foreach (string property in new[] { nameof(IsRecording), nameof(IsFinishingRecording), nameof(IsSavingRecording),
            nameof(HasRecordingToSave), nameof(CanRecord), nameof(RecordLabel) }) Changed(property);
    }

    public async Task StartRecordingAsync()
    {
        if (!CanRecord) return;
        var target = session!;
        RemoteMediaRecorder? started = null;
        started = new(() => dispatcher.InvokeAsync(() =>
        {
            if (recorder == started) _ = FinishInterruptedRecordingAsync();
        }));
        recorder = started;
        recordingSession = target;
        RecordingChanged();
        try
        {
            recordingRegistration = target.SetRemoteMediaListenerAsync(started.Append);
            await recordingRegistration;
        }
        catch (Exception error)
        {
            started.Fail(error.Message);
            await StopRecordingAsync();
        }
    }

    private async Task FinishInterruptedRecordingAsync()
    {
        try { await StopRecordingAsync(); }
        catch (Exception error) { ReportError(error); }
    }

    public Task StopRecordingAsync()
    {
        if (recordingStop is not null) return recordingStop;
        if (recorder is null) return Task.CompletedTask;
        var stopped = recorder;
        var target = recordingSession;
        var registration = recordingRegistration;
        recorder = null; recordingSession = null; recordingRegistration = null;
        stopped.StopAccepting();
        var finished = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        recordingStop = finished.Task;
        RecordingChanged();
        _ = FinishRecordingAsync(stopped, target, registration, finished);
        return finished.Task;
    }

    private async Task FinishRecordingAsync(RemoteMediaRecorder stopped, XmaxRealtime? target, Task? registration,
        TaskCompletionSource finished)
    {
        try
        {
            try { if (registration is not null) await registration; }
            catch { /* A failed registration may still require listener cleanup. */ }
            try { if (target is not null) await target.SetRemoteMediaListenerAsync(null); }
            catch (Exception error) { stopped.Fail(error.Message); }
            var result = await stopped.FinishAsync();
            if (result.Path is not null) { completedRecording = stopped; recordingPath = result.Path; }
            else await stopped.DisposeAsync();
            if (result.Error is not null) ReportError(new IOException(result.Error));
            finished.TrySetResult();
        }
        catch (Exception error) { finished.TrySetException(error); }
        finally { recordingStop = null; RecordingChanged(); }
    }

    public async Task SaveRecordingAsync(string destination)
    {
        if (recordingPath is null || savingRecording) return;
        savingRecording = true; RecordingChanged();
        try
        {
            await RemoteMediaRecorder.SaveAsync(recordingPath, destination);
            await DiscardCompletedRecordingAsync();
        }
        finally { savingRecording = false; RecordingChanged(); }
    }

    public Task DiscardRecordingAsync() => savingRecording ? Task.CompletedTask : DiscardCompletedRecordingAsync();

    private async Task DiscardCompletedRecordingAsync()
    {
        var completed = completedRecording;
        // Clear only after the file has been saved or the user explicitly discards it.
        if (completed is not null) await completed.DisposeAsync();
        completedRecording = null; recordingPath = null; RecordingChanged();
    }
}
