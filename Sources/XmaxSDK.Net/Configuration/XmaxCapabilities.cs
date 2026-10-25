namespace Xmax.SDK;

[Flags]
public enum XmaxCapabilities : ulong
{
    None = 0, Rtc = 1, Camera = 2, NativeWindow = 4, Storage = 8,
    Trajectory = 32, RemoteMediaOutput = 64, ExternalAudioInput = 128
}
