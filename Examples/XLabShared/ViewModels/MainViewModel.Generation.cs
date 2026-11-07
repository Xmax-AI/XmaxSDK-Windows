//
//  MainViewModel.Generation.cs
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
    public async Task GenerateAsync()
    {
        if (string.IsNullOrWhiteSpace(ApiKey)) throw new InvalidOperationException("实时生成需要 Xmax API Key；摄像头预览可以单独使用。");
        if (string.IsNullOrWhiteSpace(Prompt)) throw new InvalidOperationException("请输入生成提示词。");
        await EnsureSessionAsync();
        var currentSession = session ?? throw new OperationCanceledException();
        int current = revision;
        operationCancellation = new();
        try
        {
            await GenerateOnStreamAsync(
                currentSession, new(Prompt, ReferenceImage), FaceRestoration, current, operationCancellation.Token
            );
        }
        finally { if (current == revision) { operationCancellation?.Dispose(); operationCancellation = null; } }
    }

    private async Task GenerateOnStreamAsync(
        XmaxRealtime target,
        GenerationContext context,
        bool faceRestoration,
        int current,
        CancellationToken cancellationToken = default
    )
    {
        var stream = localStream ?? throw new OperationCanceledException();
        var result = await target.StartGenerationAsync(stream, context, faceRestoration, cancellationToken);
        if (current == revision)
            RemoteVideo = result.VideoTrack;
    }
}
