using Microsoft.UI.Xaml.Controls;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace XLabWinUI;

public sealed partial class MainWindow
{
    private Task? recordingSaveTask;

    private async Task ToggleRecordingAsync()
    {
        try
        {
            if (Model.IsRecording) await Model.StopRecordingAsync();
            else await Model.StartRecordingAsync();
            await PresentRecordingSaveAsync();
        }
        catch (Exception error) { Model.ReportError(error); }
    }

    private Task PresentRecordingSaveAsync()
    {
        if (recordingSaveTask is not null) return recordingSaveTask;
        if (!Model.HasRecordingToSave || Model.IsFinishingRecording) return Task.CompletedTask;
        var finished = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        recordingSaveTask = finished.Task;
        _ = SaveRecordingWithPickerAsync(finished);
        return finished.Task;
    }

    private async Task SaveRecordingWithPickerAsync(TaskCompletionSource finished)
    {
        try
        {
            ClosePopups();
            while (Model.HasRecordingToSave)
            {
                try
                {
                    var picker = new FileSavePicker
                    {
                        SuggestedStartLocation = PickerLocationId.VideosLibrary,
                        SuggestedFileName = "XLab-" + DateTime.Now.ToString("yyyyMMdd-HHmmss")
                    };
                    picker.FileTypeChoices.Add("MPEG-4 video", new List<string> { ".mp4" });
                    InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(this));
                    var file = await picker.PickSaveFileAsync();
                    if (file is null) await Model.DiscardRecordingAsync();
                    else await Model.SaveRecordingAsync(file.Path);
                }
                catch (Exception error)
                {
                    var retry = new ContentDialog
                    {
                        XamlRoot = root.XamlRoot,
                        Title = "Unable to Save Recording",
                        Content = error.Message + "\nYour recording is still available. Try another location or discard it.",
                        PrimaryButtonText = "Try Again",
                        CloseButtonText = "Discard Recording",
                        DefaultButton = ContentDialogButton.Primary
                    };
                    if (await retry.ShowAsync() != ContentDialogResult.Primary) await Model.DiscardRecordingAsync();
                }
            }
            finished.TrySetResult();
        }
        catch (Exception error) { Model.ReportError(error); finished.TrySetException(error); }
        finally { recordingSaveTask = null; RefreshUi(); }
    }
}
