//
//  MediaDevices.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using Xmax.SDK.Interop;

namespace Xmax.SDK;

/// <summary>Reads capture devices without opening capture or creating a realtime controller.</summary>
public static class MediaDevices
{
    /// <summary>Reads a fresh snapshot of camera devices without requesting permission.</summary>
    public static Task<IReadOnlyList<CameraDevice>> ListCameraDevicesAsync(
        CancellationToken cancellationToken = default
    ) => Task.Run(ReadCameraSnapshot, cancellationToken);

    /// <summary>Reads a fresh snapshot of recording devices without requesting permission.</summary>
    public static Task<IReadOnlyList<MicrophoneDevice>> ListMicrophoneDevicesAsync(
        CancellationToken cancellationToken = default
    ) => Task.Run(ReadMicrophoneSnapshot, cancellationToken);

    internal static IReadOnlyList<CameraDevice> ReadCameraSnapshot() => ReadSnapshot(ReadCameras);
    internal static IReadOnlyList<MicrophoneDevice> ReadMicrophoneSnapshot() => ReadSnapshot(ReadMicrophones);

    private static IReadOnlyList<T> ReadSnapshot<T>(Func<IReadOnlyList<T>> read)
    {
        NativeLibraryLoader.Initialize();
        try { return read(); }
        catch (XmaxException error) when (error.Code is XmaxErrorCode.MediaError
            or XmaxErrorCode.CameraPermissionDenied or XmaxErrorCode.MicrophonePermissionDenied)
        {
            return Array.Empty<T>();
        }
    }

    private static unsafe IReadOnlyList<CameraDevice> ReadCameras()
    {
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_media_list_cameras(&value));
        return ReadCameraList(value);
    }

    private static unsafe IReadOnlyList<CameraDevice> ReadCameraList(nint value)
    {
        using var list = new NativeHandle(value, HandleKind.CameraList);
        nuint count = NativeMethods.xmax_camera_list_count(value);
        var result = new List<CameraDevice>();
        for (nuint i = 0; i < count; ++i)
        {
            var info = new xmax_camera_info { struct_size = (uint)sizeof(xmax_camera_info) };
            NativeError.Check(NativeMethods.xmax_camera_list_get(value, i, &info));
            int position;
            NativeError.Check(NativeMethods.xmax_camera_list_get_position(
                value,
                i,
                &position
            ));
            result.Add(new CameraDevice(
                NativeError.Text(info.id),
                NativeError.Text(info.name),
                info.is_default != 0
            ) { Position = (CameraPosition)position });
        }
        return result.AsReadOnly();
    }

    private static unsafe IReadOnlyList<MicrophoneDevice> ReadMicrophones()
    {
        nint value = 0;
        NativeError.Check(NativeMethods.xmax_media_list_microphones(&value));
        using var list = new NativeHandle(value, HandleKind.CameraList);
        var result = new List<MicrophoneDevice>();
        nuint count = NativeMethods.xmax_camera_list_count(value);
        for (nuint index = 0; index < count; ++index)
        {
            var info = new xmax_camera_info { struct_size = (uint)sizeof(xmax_camera_info) };
            NativeError.Check(NativeMethods.xmax_camera_list_get(value, index, &info));
            result.Add(new(NativeError.Text(info.id), NativeError.Text(info.name), info.is_default != 0));
        }
        return Array.AsReadOnly(result.OrderBy(device => device.Name, StringComparer.CurrentCultureIgnoreCase).ToArray());
    }
}
