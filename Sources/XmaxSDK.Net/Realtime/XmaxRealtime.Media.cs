//
//  XmaxRealtime.Media.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;
using Xmax.SDK.Runtime;

namespace Xmax.SDK;

internal sealed partial class XmaxRealtime
{

    /// <summary>
    /// Registers one serialized listener for result-user PCM and generated video after SDK beauty on a shared rendering timeline.
    /// </summary>
    /// <param name="listener">The background callback owning each received packet; dispose or transfer every packet. Null clears the listener and queued output.</param>
    /// <returns>A task completing after listener registration changes.</returns>
    /// <remarks>
    /// Registration may wait for lifecycle work and does not change playback volume or subscriptions.
    /// An in-flight callback may finish after replacement or removal; captured context stays alive through that callback.
    /// Forward packets promptly to avoid bounded-queue drops. Callback exceptions are logged.
    /// </remarks>
    public Task SetRemoteMediaFrameListenerAsync(
        Action<RealtimeMediaFrame>? listener
    ) =>
        operations.Run(value => { RemoteMediaRegistration.Set(value, listener, NotifyCallbackFault); return 0; });

    /// <summary>
    /// Sets remote playback volume for the current or next connection.
    /// </summary>
    /// <param name="volume">A finite volume from 0 through 1; 0 mutes playback.</param>
    /// <returns>A task completing after the playback setting is applied.</returns>
    /// <remarks>
    /// Playback starts at 0. This control does not wait for native connection or generation admission and does not change recorded PCM.
    /// </remarks>
    public Task SetRemoteVolumeAsync(
        float volume
    ) => Run(value => NativeError.Check(NativeMethods.xmax_realtime_set_remote_volume(value, volume)));

    /// <summary>
    /// Reads the playback volume retained for the current or next connection.
    /// </summary>
    /// <returns>A task containing a volume from 0 through 1, initially 0.</returns>
    /// <remarks>
    /// This query does not wait for native connection or generation admission.
    /// </remarks>
    public Task<float> GetRemoteVolumeAsync() => operations.Run(GetVolume);

    private static unsafe float GetVolume(nint value)
    { float volume; NativeError.Check(NativeMethods.xmax_realtime_get_remote_volume(value, &volume)); return volume; }
}
