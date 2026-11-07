//
//  XmaxRealtime.ExternalVideo.cs
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
    /// <summary>Starts an app-owned video source and returns a local stream for preview.</summary>
    /// <param name="source">The external source providing video and optional PCM.</param>
    /// <param name="videoFormat">The optional processing format. Omit it to use the source format.</param>
    /// <param name="cancellationToken">Cancels binding and source startup.</param>
    /// <returns>A stream ready for preview once the source installs frame delivery.</returns>
    /// <remarks>
    /// Stop existing local media before creating a stream. Submit frames at the declared source
    /// dimensions. The SDK resizes to the model-aligned processing format. Disconnecting retains
    /// source delivery for local preview. Await this method before starting RTC or generation.
    /// </remarks>
    public async Task<RealtimeMediaStream> CreateExternalVideoStreamAsync(
        IRealtimeExternalVideoSource source,
        VideoFormat? videoFormat = null,
        CancellationToken cancellationToken = default
    )
    {
        ArgumentNullException.ThrowIfNull(source);
        await localMediaGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            using var startup = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            lock (externalStartupGate)
            {
                ObjectDisposedException.ThrowIf(disposingLocalMedia, this);
                if (externalCloseCount != 0)
                    throw new OperationCanceledException("The controller is closing local media.");
                externalStartup = startup;
            }

            try
            {
                await DetachInactiveExternalSourceAsync().ConfigureAwait(false);
                var format = source.VideoFormat;
                var hasAudio = source.HasAudio;
                var sink = await operations.Run(
                    value => BindExternalStream(value, format, videoFormat, hasAudio),
                    startup.Token
                ).ConfigureAwait(false);
                var attachment = new ExternalAttachment(source, sink);
                Volatile.Write(ref externalAttachment, attachment);
                try
                {
                    startup.Token.ThrowIfCancellationRequested();
                    await source.StartAsync(sink, startup.Token).ConfigureAwait(false);
                    startup.Token.ThrowIfCancellationRequested();
                    var stream = await operations.Run(value =>
                    {
                        sink.CompleteStartup();
                        return CreateLocalStream(value);
                    }, startup.Token).ConfigureAwait(false);
                    startup.Token.ThrowIfCancellationRequested();

                    return stream;
                }
                catch
                {
                    try { await Run(StopExternalNative).ConfigureAwait(false); }
                    catch (ObjectDisposedException) { }
                    catch (Exception error) { NotifyCallbackFault(error); }
                    await DetachExternalSourceAsync().ConfigureAwait(false);
                    throw;
                }
            }
            finally
            {
                Task? cancelling;
                lock (externalStartupGate)
                {
                    externalStartup = null;
                    cancelling = externalStartupCancellation;
                    externalStartupCancellation = null;
                }
                if (cancelling is not null)
                    await ObserveExternalCancellationAsync(cancelling).ConfigureAwait(false);
            }
        }
        finally
        {
            localMediaGate.Release();
        }
    }

    /// <summary>Stops external frame delivery and clears the local preview after disconnection.</summary>
    /// <returns>A task completing after the source releases its SDK receiver.</returns>
    public Task StopExternalVideoStreamAsync()
    {
        lock (externalStartupGate)
            _ = CancelExternalStartup();
        return ChangeLocalMediaAsync(() => Run(StopExternalNative));
    }

    private static void StopExternalNative(nint value) =>
        NativeError.Check(NativeMethods.xmax_realtime_stop_external_video_stream(value));

    private static unsafe ExternalFrameSink BindExternalStream(
        nint realtime,
        VideoFormat sourceFormat,
        VideoFormat? processingFormat,
        bool hasAudio
    )
    {
        var source = new xmax_external_media_config
        {
            struct_size = (uint)sizeof(xmax_external_media_config),
            video_format = sourceFormat.Native,
            has_audio = hasAudio ? 1 : 0
        };
        var requested = processingFormat.GetValueOrDefault().Native;
        var encoding = (processingFormat ?? sourceFormat).NativeEncoding;
        xmax_video_format resolved;
        nint sink = 0;
        NativeError.Check(NativeMethods.xmax_realtime_create_external_video_stream_with_encoding(
            realtime,
            &source,
            processingFormat.HasValue ? &requested : null,
            &resolved,
            &sink,
            &encoding
        ));
        return new(new(sink, HandleKind.ExternalSink), sourceFormat, VideoFormat.From(resolved).WithEncoding(encoding));
    }
}
