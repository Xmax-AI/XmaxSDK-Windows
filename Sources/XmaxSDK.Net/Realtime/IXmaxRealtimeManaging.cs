//
//  IXmaxRealtimeManaging.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

namespace Xmax.SDK;

/// <summary>Manages local media, the realtime connection, and generation for one controller.</summary>
/// <remarks>Dispose asynchronously when the controller is no longer needed. Close releases the current
/// session and local media so the controller can be reused.</remarks>
public interface IXmaxRealtimeManaging : IAsyncDisposable
{
    /// <summary>Gets the configuration supplied when creating this controller.</summary>
    RealtimeConfiguration Configuration { get; }
    /// <summary>
    /// Reads lifecycle state, identifiers, state reason, and associated error without consuming events.
    /// </summary>
    /// <returns>A task containing the state snapshot.</returns>
    /// <remarks>
    /// The snapshot is available while connection, generation, or cleanup is in progress.
    /// </remarks>
    Task<RealtimeState> GetStateAsync();

    /// <summary>Lists the available camera devices without starting capture.</summary>
    Task<IReadOnlyList<CameraDevice>> ListCameraDevicesAsync(CancellationToken cancellationToken = default);
    /// <summary>Lists recording endpoints without starting capture or requesting permission.</summary>
    Task<IReadOnlyList<MicrophoneDevice>> ListMicrophoneDevicesAsync(CancellationToken cancellationToken = default);
    /// <summary>Creates a local camera track without opening a server session. Omitted selections use
    /// the model default format and system camera, with the microphone disabled.</summary>
    Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        VideoFormat videoFormat = default, RealtimeCamera? camera = null, RealtimeMicrophone? microphone = null,
        bool mirror = false, CancellationToken cancellationToken = default);
    /// <summary>Creates camera media using a resolution preset and its model-defined frame rate.</summary>
    /// <param name="preset">A camera resolution preset supported by the selected model.</param>
    /// <param name="camera">The camera selection; null uses the system default.</param>
    /// <param name="microphone">The initial microphone selection; null disables capture.</param>
    /// <param name="mirror">Mirrors local preview and publication together.</param>
    /// <param name="cancellationToken">Cancels preparation and releases local media.</param>
    /// <returns>The prepared local camera stream.</returns>
    Task<RealtimeMediaStream> CreateLocalCameraStreamAsync(
        RealtimeCameraPreset preset, RealtimeCamera? camera = null, RealtimeMicrophone? microphone = null,
        bool mirror = false, CancellationToken cancellationToken = default);
    /// <summary>Stops local camera capture after disconnection.</summary>
    Task StopLocalCameraStreamAsync();
    /// <summary>
    /// Switches camera capture while retaining the connection and restoring generation conditions.
    /// </summary>
    /// <param name="deviceId">The identifier of an enumerated camera.</param>
    /// <returns>A task completing when capture and any generation restart finish.</returns>
    /// <remarks>
    /// The operation waits for the new camera and restarts any active generation. If the camera fails,
    /// the SDK attempts to restore local capture. A failure during generation closes the connection.
    /// </remarks>
    Task<RealtimeMediaStream> SwitchCameraAsync(string deviceId);
    /// <summary>Reads the active camera format, or null when no camera is active.</summary>
    Task<VideoFormat?> GetCurrentCameraVideoFormatAsync();
    /// <summary>Reads the resolution preset of the active camera.</summary>
    Task<RealtimeCameraPreset?> GetCurrentCameraPresetAsync();
    /// <summary>
    /// Changes a ready, disconnected camera encoding target while retaining the device and preview binding.
    /// </summary>
    /// <param name="format">The model-supported target dimensions and nominal frame rate.</param>
    /// <returns>A task completing after reconfiguration or recovery.</returns>
    /// <remarks>
    /// Preparation failure attempts to restore the previous format and reports the original error.
    /// </remarks>
    Task UpdateCameraFormatAsync(VideoFormat format);
    /// <summary>Changes camera dimensions while preserving the current frame rate and encoding options.</summary>
    Task UpdateCameraFormatAsync(RealtimeCameraPreset preset);

    /// <summary>Binds an application-owned source without taking ownership of its capture devices.</summary>
    Task<RealtimeMediaStream> CreateExternalVideoStreamAsync(
        IRealtimeExternalVideoSource source, VideoFormat? videoFormat = null, CancellationToken cancellationToken = default);
    /// <summary>Stops external frame delivery and clears the local preview after disconnection.</summary>
    /// <returns>A task completing after the source releases its SDK receiver.</returns>
    Task StopExternalVideoStreamAsync();

    /// <summary>Reads whether the camera stream has enabled microphone capture.</summary>
    Task<bool> GetMicrophoneEnabledAsync();
    /// <summary>Changes camera microphone capture and publication without restarting video generation.</summary>
    Task SetMicrophoneEnabledAsync(bool enabled);
    /// <summary>Changes the selected camera microphone without restarting video generation.</summary>
    /// <param name="deviceId">The Windows recording endpoint ID, or null to follow the default.</param>
    Task SwitchMicrophoneAsync(string? deviceId);
    /// <summary>
    /// Reads the playback volume retained for the current or next connection.
    /// </summary>
    /// <returns>A task containing a volume from 0 through 1, initially 0.</returns>
    /// <remarks>
    /// This query does not wait for native connection or generation admission.
    /// </remarks>
    Task<float> GetRemoteVolumeAsync();
    /// <summary>
    /// Sets remote playback volume for the current or next connection.
    /// </summary>
    /// <param name="volume">A finite volume from 0 through 1; 0 mutes playback.</param>
    /// <returns>A task completing after the playback setting is applied.</returns>
    /// <remarks>
    /// Playback starts at 0. This control does not wait for native connection or generation admission and does not change recorded PCM.
    /// </remarks>
    Task SetRemoteVolumeAsync(float volume);
    /// <summary>Configures result beauty; null disables processing and releases its helper.</summary>
    /// <remarks>
    /// Configure before connecting or during generation. Reconnection retains options; CloseAsync
    /// restores enabled, zero-strength defaults. Invalid options are rejected before changing processing.
    /// Initialization/configuration failures throw here; later frame failures notify the independent listener.
    /// </remarks>
    Task SetRemoteVideoBeautyAsync(RealtimeVideoBeautyOptions? options);
    /// <summary>
    /// Reads the applied uplink quality level.
    /// </summary>
    /// <returns>The level, or null while the RTC connection is inactive.</returns>
    Task<RealtimeQualityLevel?> GetUplinkQualityLevelAsync();
    /// <summary>
    /// Reads the applied downlink quality level.
    /// </summary>
    /// <returns>The level, or null while the RTC connection is inactive.</returns>
    Task<RealtimeQualityLevel?> GetDownlinkQualityLevelAsync();
    /// <summary>
    /// Applies an uplink quality target during generation.
    /// </summary>
    /// <param name="level">The transport quality target.</param>
    /// <returns>A task completing when the target has been applied.</returns>
    Task SetUplinkQualityLevelAsync(RealtimeQualityLevel level);
    /// <summary>Applies a downlink quality target during generation.</summary>
    Task SetDownlinkQualityLevelAsync(RealtimeQualityLevel level);

    /// <summary>Registers a state observer and awaits delivery of the current state.</summary>
    Task SetStateListenerAsync(Action<RealtimeState>? listener, SynchronizationContext? callbackContext = null);
    /// <summary>Registers a microphone observer and awaits delivery of the current reading, if any.</summary>
    Task SetMicrophoneLevelListenerAsync(Action<float?>? listener);
    /// <summary>Registers a generation startup timing listener, or clears it when null.</summary>
    Task SetLaunchTimingListenerAsync(Action<RealtimeLaunchTiming>? listener);
    /// <summary>Registers the uplink video statistics listener, for future snapshots, including unavailable measurements.</summary>
    Task SetUplinkVideoStatisticsListenerAsync(Action<UplinkVideoStatistics?>? listener);
    /// <summary>Registers the downlink video statistics listener, for future snapshots, including unavailable measurements.</summary>
    Task SetDownlinkVideoStatisticsListenerAsync(Action<DownlinkVideoStatistics?>? listener);
    /// <summary>Registers a directional network quality listener, or clears it when null.</summary>
    Task SetNetworkQualityListenerAsync(Action<RealtimeNetworkQuality>? listener);
    /// <summary>Registers a device performance alarm listener, or clears it when null.</summary>
    Task SetPerformanceAlarmListenerAsync(Action<RealtimePerformanceAlarm>? listener);
    /// <summary>Registers recoverable beauty failures after the SDK restores original-frame output.</summary>
    /// <remarks>
    /// Uses the controller or registering synchronization context; otherwise uses the thread pool.
    /// Callback exceptions are logged. Null unregisters; full close also clears this listener.
    /// A queued notification is discarded when its listener is released. Generation errors use the existing events.
    /// </remarks>
    Task SetRemoteVideoBeautyErrorListenerAsync(Action<XmaxException>? listener);
    /// <summary>Receives serialized result media on a background worker. The listener owns each packet.</summary>
    Task SetRemoteMediaFrameListenerAsync(Action<RealtimeMediaFrame>? listener);

    /// <summary>Connects a local stream owned by this controller and returns the result stream.</summary>
    /// <param name="localStream">The current local media stream.</param>
    /// <param name="cancellationToken">Cancels this connection attempt.</param>
    Task<RealtimeMediaStream> ConnectAsync(RealtimeMediaStream localStream, CancellationToken cancellationToken = default);
    /// <summary>Starts or updates generation on the current connection.</summary>
    /// <remarks>Connect a local stream first. Initial generation requires a context.</remarks>
    /// <param name="context">Initial conditions, or null to reuse successful conditions.</param>
    /// <param name="faceRestoration">Whether to restore the original face for this request.</param>
    /// <param name="cancellationToken">Cancels this generation request.</param>
    Task StartGenerationAsync(
        GenerationContext? context = null, bool faceRestoration = false, CancellationToken cancellationToken = default);
    /// <summary>Connects and generates using a specific local stream and returns the result stream.</summary>
    /// <param name="localStream">The current local media stream.</param>
    /// <param name="context">Initial conditions, or null to reuse successful conditions.</param>
    /// <param name="faceRestoration">Whether to restore the original face for this request.</param>
    /// <param name="cancellationToken">Cancels this generation request.</param>
    Task<RealtimeMediaStream> StartGenerationAsync(
        RealtimeMediaStream localStream, GenerationContext? context = null, bool faceRestoration = false,
        CancellationToken cancellationToken = default);
    /// <summary>
    /// Disconnects the server session and RTC, clears generation conditions, and retains local preview.
    /// </summary>
    /// <returns>A task completing after disconnection.</returns>
    /// <remarks>
    /// Requests cancellation of in-flight lifecycle work. Prepared local media can be used for reconnection.
    /// Service cleanup failures are logged without replacing the disconnect reason.
    /// </remarks>
    Task DisconnectAsync();
    /// <summary>Disconnects for a business reason while retaining local preview.</summary>
    /// <param name="reason">A normal disconnect, an orientation change, or an SDK failure.</param>
    Task DisconnectAsync(RealtimeReason reason);
    /// <summary>
    /// Disconnects and releases local media and cached generation conditions.
    /// </summary>
    /// <returns>A task completing after native resources are closed.</returns>
    /// <remarks>
    /// The controller remains usable: prepare media again before starting a new session. This method requests cancellation of in-flight startup.
    /// </remarks>
    Task CloseAsync();
}
