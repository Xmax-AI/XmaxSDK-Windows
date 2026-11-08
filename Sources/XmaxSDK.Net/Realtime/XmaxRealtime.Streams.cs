//
//  XmaxRealtime.Streams.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xmax.SDK.Interop;

namespace Xmax.SDK;

internal sealed partial class XmaxRealtime
{
    /// <summary>Connects a local stream owned by this controller and returns the result stream.</summary>
    /// <param name="localStream">The current local media stream.</param>
    /// <param name="cancellationToken">Cancels this connection attempt.</param>
    public Task<RealtimeMediaStream> ConnectAsync(
        RealtimeMediaStream localStream,
        CancellationToken cancellationToken = default
    )
    {
        ValidateLocalStream(localStream);
        return operations.Run(value => ConnectStream(value, localStream.Revision, cancellationToken), cancellationToken);
    }

    /// <summary>Starts or updates generation on the current connection.</summary>
    /// <remarks>Connect a local stream first. Initial generation requires a context.</remarks>
    /// <param name="context">Initial conditions, or null to reuse successful conditions.</param>
    /// <param name="faceRestoration">Whether to restore the original face for this request.</param>
    /// <param name="cancellationToken">Cancels this generation request.</param>
    public Task StartGenerationAsync(
        GenerationContext? context = null,
        bool faceRestoration = false,
        CancellationToken cancellationToken = default
    ) => RunGenerationUpdate(context, faceRestoration, cancellationToken);

    private Task RunGenerationUpdate(GenerationContext? context, bool faceRestoration, CancellationToken cancellationToken) =>
        operations.Run(value =>
        {
            ulong revision = StartStream(value, 0, context, faceRestoration, cancellationToken);
            ResetResultInteraction(revision);
            return revision;
        }, cancellationToken);

    /// <summary>Connects and generates using a specific local stream and returns the result stream.</summary>
    /// <param name="localStream">The current local media stream.</param>
    /// <param name="context">Initial conditions, or null to reuse successful conditions.</param>
    /// <param name="faceRestoration">Whether to restore the original face for this request.</param>
    /// <param name="cancellationToken">Cancels this generation request.</param>
    public Task<RealtimeMediaStream> StartGenerationAsync(
        RealtimeMediaStream localStream,
        GenerationContext? context = null,
        bool faceRestoration = false,
        CancellationToken cancellationToken = default
    )
    {
        ValidateLocalStream(localStream);
        return operations.Run(value =>
        {
            ulong revision = StartStream(value, localStream.Revision, context, faceRestoration, cancellationToken);
            ResetResultInteraction(revision);
            return CreateRemoteStream(value, revision);
        }, cancellationToken);
    }

    private void ResetResultInteraction(ulong revision)
    {
        if (Volatile.Read(ref currentRemoteStream) is { } stream && stream.Revision == revision)
            stream.VideoTrack.ResetInteraction();
    }

    private void ValidateLocalStream(RealtimeMediaStream localStream)
    {
        ArgumentNullException.ThrowIfNull(localStream);
        if (!ReferenceEquals(localStream.Owner, this) || !localStream.IsLocal)
            throw new ArgumentException("A local stream owned by this controller is required.", nameof(localStream));
    }

    private unsafe RealtimeMediaStream ConnectStream(nint value, ulong revision, CancellationToken token)
    {
        ulong remote;
        var cancellation = GCHandle.Alloc(token);
        try
        {
            int result = NativeMethods.xmax_realtime_connect_stream_cancellable(
                value, revision, &remote, &IsRequestCancelled, (void*)GCHandle.ToIntPtr(cancellation)
            );
            if (result < 0) token.ThrowIfCancellationRequested();
            NativeError.Check(result);
            return CreateRemoteStream(value, remote);
        }
        finally { cancellation.Free(); }
    }

    private static unsafe ulong StartStream(
        nint value,
        ulong revision,
        GenerationContext? context,
        bool faceRestoration,
        CancellationToken cancellationToken
    )
    {
        using var prompt = new Utf8(context?.Prompt);
        using var reference = new Utf8(context?.ReferencePath);
        var native = new xmax_generation_context
        {
            struct_size = (uint)sizeof(xmax_generation_context),
            prompt = prompt.Pointer,
            reference_path = reference.Pointer,
            face_restoration = faceRestoration ? 1 : 0
        };
        ulong remote;
        var cancellation = GCHandle.Alloc(cancellationToken);
        try
        {
            int result = NativeMethods.xmax_realtime_start_generation_cancellable(
                value, revision, context is null ? null : &native, faceRestoration ? 1 : 0,
                &remote, 0, &IsRequestCancelled, (void*)GCHandle.ToIntPtr(cancellation));
            if (result < 0) cancellationToken.ThrowIfCancellationRequested();
            NativeError.Check(result);
            return remote;
        }
        finally { cancellation.Free(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int IsRequestCancelled(nint context) =>
        ((CancellationToken)GCHandle.FromIntPtr(context).Target!).IsCancellationRequested ? 1 : 0;
}
