using System.Diagnostics;
using System.Runtime.InteropServices;

namespace XLab.Shared.Recording;

/// <summary>Encodes H.264/AAC MP4 on its owning MTA worker using Windows Media Foundation.</summary>
internal sealed unsafe class MediaFoundationWriter : IRecordingWriter
{
    private nint writer;
    private bool com, foundation;
    private uint videoTrack, audioTrack;
    private int canvasWidth, canvasHeight;
    private long? audioOrigin;
    private long audioSamples;
    private readonly byte[] silence = new byte[9600];
    private static readonly Guid Major = new("48eba18e-f8c9-4687-bf11-0a74c9f96a8f"), Subtype = new("f7e34c9a-42e8-4714-b74b-cb29d72c35e5");
    private static readonly Guid VideoType = new("73646976-0000-0010-8000-00aa00389b71"), AudioType = new("73647561-0000-0010-8000-00aa00389b71");
    private static readonly Guid H264 = new("34363248-0000-0010-8000-00aa00389b71"), Rgb32 = new("00000016-0000-0010-8000-00aa00389b71");
    private static readonly Guid Aac = new("00001610-0000-0010-8000-00aa00389b71"), Pcm = new("00000001-0000-0010-8000-00aa00389b71");
    private static readonly Guid FrameSize = new("1652c33d-d6b2-4012-b834-72030849a37d"), FrameRate = new("c459a2e8-3d2c-4e44-b132-fee5156c7bb0");
    private static readonly Guid Aspect = new("c6376a1e-8d0a-4027-be45-6d9a0ad39bb6"), Stride = new("644b4e48-1e02-4516-b0eb-c01ca9d49ac6");
    private static readonly Guid Bitrate = new("20332624-fb0d-4d9e-bd0d-cbf6786c102e"), Interlace = new("e2724bb8-e676-4806-b4b2-a8d6efb44ccd");
    private static readonly Guid Rate = new("5faeeae7-0290-4c31-9e8a-c534f68d9dba"), Channels = new("37e48bf5-645e-4c5b-89de-ada9e29b696a");
    private static readonly Guid Bits = new("f2deb57f-40fa-4764-aa33-ed4f2d1ff669"), Bytes = new("1aab75c8-cfef-451c-ab95-ac034b8e1731");
    private static readonly Guid Alignment = new("322de230-9eeb-43bd-ab7a-ff412251541d"), NoThrottle = new("08b845d8-2b74-4afe-9d53-be16d2d5ae4f");
    internal static nint Slot(nint instance, int index) => ((nint*)*(nint*)instance)[index];
    internal static void Check(int status, string action)
    { if (status < 0) throw new COMException($"{action} (0x{status:X8}).", status); }
    internal static void Release(ref nint value) { nint owned = value; value = 0; if (owned != 0) Marshal.Release(owned); }
    private static void U32(nint type, Guid key, uint value) => Check(((delegate* unmanaged[Stdcall]<nint, Guid*, uint, int>)Slot(type, 21))(type, &key, value), "Configure recording attribute");
    private static void U64(nint type, Guid key, uint a, uint b) => Check(((delegate* unmanaged[Stdcall]<nint, Guid*, ulong, int>)Slot(type, 22))(type, &key, ((ulong)a << 32) | b), "Configure recording dimensions");
    private static void Id(nint type, Guid key, Guid value) => Check(((delegate* unmanaged[Stdcall]<nint, Guid*, Guid*, int>)Slot(type, 24))(type, &key, &value), "Configure recording media type");
    private static void Clone(ref nint type)
    {
        nint input = 0;
        try
        {
            Check(MFCreateMediaType(out input), "Create recording input type");
            Check(((delegate* unmanaged[Stdcall]<nint, nint, int>)Slot(type, 32))(type, input), "Copy recording media attributes");
        }
        catch { Release(ref input); throw; }
        Release(ref type); type = input;
    }
    private uint Add(nint type)
    {
        uint index = 0;
        Check(((delegate* unmanaged[Stdcall]<nint, nint, uint*, int>)Slot(writer, 3))(writer, type, &index), "Add MP4 track");
        return index;
    }
    private void Input(uint track, nint type) => Check(((delegate* unmanaged[Stdcall]<nint, uint, nint, nint, int>)Slot(writer, 4))(writer, track, type, 0), "Initialize recording encoder");
    public void Open(string path, int width, int height)
    {
        Check(CoInitializeEx(0, 0), "Initialize recording COM"); com = true;
        Check(MFStartup(0x20070, 0), "Initialize Media Foundation"); foundation = true;
        canvasWidth = width; canvasHeight = height;
        nint attributes = 0, type = 0;
        try
        {
            Check(MFCreateAttributes(out attributes, 1), "Create MP4 settings"); U32(attributes, NoThrottle, 1);
            Check(MFCreateSinkWriterFromURL(path, 0, attributes, out writer), "Open MP4 recording");
            Check(MFCreateMediaType(out type), "Create video track");
            Id(type, Major, VideoType); Id(type, Subtype, H264);
            U32(type, Bitrate, (uint)Math.Clamp((long)width * height * 6, 2_000_000, 20_000_000)); U32(type, Interlace, 2);
            U64(type, FrameSize, (uint)width, (uint)height); U64(type, FrameRate, 30, 1); U64(type, Aspect, 1, 1);
            videoTrack = Add(type); Clone(ref type);
            Id(type, Subtype, Rgb32); U32(type, Stride, checked((uint)width * 4)); Input(videoTrack, type); Release(ref type);
            Check(MFCreateMediaType(out type), "Create audio track");
            Id(type, Major, AudioType); Id(type, Subtype, Aac);
            U32(type, Rate, 48000); U32(type, Channels, 1); U32(type, Bits, 16); U32(type, Bytes, 16000); U32(type, Alignment, 1);
            audioTrack = Add(type); Clone(ref type);
            Id(type, Subtype, Pcm); U32(type, Bytes, 96000); U32(type, Alignment, 2); Input(audioTrack, type);
            Check(((delegate* unmanaged[Stdcall]<nint, int>)Slot(writer, 5))(writer), "Start MP4 recording");
        }
        finally { Release(ref attributes); Release(ref type); }
    }
    private void Write(uint track, int length, long time, long duration, Action<nint> copy)
    {
        nint buffer = 0, sample = 0;
        try
        {
            Check(MFCreateMemoryBuffer(checked((uint)length), out buffer), "Allocate recording sample");
            byte* bytes = null;
            Check(((delegate* unmanaged[Stdcall]<nint, byte**, uint*, uint*, int>)Slot(buffer, 3))(buffer, &bytes, null, null), "Access recording sample");
            try { copy((nint)bytes); }
            finally { Check(((delegate* unmanaged[Stdcall]<nint, int>)Slot(buffer, 4))(buffer), "Release recording sample"); }
            Check(((delegate* unmanaged[Stdcall]<nint, uint, int>)Slot(buffer, 6))(buffer, (uint)length), "Set recording sample length");
            Check(MFCreateSample(out sample), "Create recording sample");
            Check(((delegate* unmanaged[Stdcall]<nint, nint, int>)Slot(sample, 42))(sample, buffer), "Attach recording sample");
            Check(((delegate* unmanaged[Stdcall]<nint, long, int>)Slot(sample, 36))(sample, time), "Set recording sample time");
            Check(((delegate* unmanaged[Stdcall]<nint, long, int>)Slot(sample, 38))(sample, duration), "Set recording sample duration");
            Check(((delegate* unmanaged[Stdcall]<nint, uint, nint, int>)Slot(writer, 6))(writer, track, sample), "Encode recording sample");
        }
        finally { Release(ref sample); Release(ref buffer); }
    }
    public void Video(nint pixels, int stride, int size, int width, int height, long time, long duration)
    {
        if (width < 2 || height < 2 || stride < checked(width * 4) || size < checked((long)stride * (height - 1) + width * 4))
            throw new InvalidDataException("Invalid recording video layout.");
        int bytes = checked(canvasWidth * canvasHeight * 4);
        Write(videoTrack, bytes, time, duration, destination =>
        {
            byte* source = (byte*)pixels, target = (byte*)destination;
            if (width == canvasWidth && height == canvasHeight)
            {
                for (int y = 0; y < height; ++y) Buffer.MemoryCopy(source + (long)y * stride, target + (long)y * width * 4, width * 4, width * 4);
                return;
            }
            // Keep one fixed MP4 canvas when quality changes; preserve aspect ratio with black padding.
            new Span<byte>(target, bytes).Clear();
            double scale = Math.Min((double)canvasWidth / width, (double)canvasHeight / height);
            int fittedWidth = Math.Max(1, (int)Math.Round(width * scale)), fittedHeight = Math.Max(1, (int)Math.Round(height * scale));
            int left = (canvasWidth - fittedWidth) / 2, top = (canvasHeight - fittedHeight) / 2;
            for (int y = 0; y < fittedHeight; ++y)
            {
                double fy = Math.Clamp((y + .5) / scale - .5, 0, height - 1); int y0 = (int)fy, y1 = Math.Min(y0 + 1, height - 1);
                for (int x = 0; x < fittedWidth; ++x)
                {
                    double fx = Math.Clamp((x + .5) / scale - .5, 0, width - 1); int x0 = (int)fx, x1 = Math.Min(x0 + 1, width - 1);
                    for (int c = 0; c < 3; ++c)
                    {
                        double a = source[(long)y0 * stride + x0 * 4 + c] * (1 - (fx - x0)) + source[(long)y0 * stride + x1 * 4 + c] * (fx - x0);
                        double b = source[(long)y1 * stride + x0 * 4 + c] * (1 - (fx - x0)) + source[(long)y1 * stride + x1 * 4 + c] * (fx - x0);
                        target[((long)(top + y) * canvasWidth + left + x) * 4 + c] = (byte)Math.Clamp((int)Math.Round(a * (1 - (fy - y0)) + b * (fy - y0)), 0, 255);
                    }
                }
            }
        });
    }
    public void Audio(ReadOnlySpan<byte> pcm, long time)
    {
        audioOrigin ??= time;
        long target = (long)Math.Round((time - audioOrigin.Value) * 48000d / 10_000_000);
        var deadline = Stopwatch.StartNew();
        while (audioSamples < target)
        {
            if (deadline.ElapsedMilliseconds > 500) throw new TimeoutException("The recording encoder could not keep up. Save the recorded portion.");
            int count = (int)Math.Min(target - audioSamples, 4800);
            WriteAudio(silence.AsSpan(0, count * 2));
        }
        WriteAudio(pcm);
    }
    private void WriteAudio(ReadOnlySpan<byte> pcm)
    {
        fixed (byte* bytes = pcm)
        {
            nint data = (nint)bytes; int length = pcm.Length;
            Write(audioTrack, length, audioOrigin!.Value + audioSamples * 10_000_000 / 48000, length / 2L * 10_000_000 / 48000,
                destination => Buffer.MemoryCopy((void*)data, (void*)destination, length, length));
            audioSamples += length / 2;
        }
    }
    public void Finish() => Check(((delegate* unmanaged[Stdcall]<nint, int>)Slot(writer, 11))(writer), "Finalize MP4 recording");
    public void Dispose()
    {
        Release(ref writer);
        if (foundation) { foundation = false; MFShutdown(); }
        if (com) { com = false; CoUninitialize(); }
    }
    [DllImport("ole32.dll")] private static extern int CoInitializeEx(nint reserved, uint mode);
    [DllImport("ole32.dll")] private static extern void CoUninitialize();
    [DllImport("mfplat.dll")] private static extern int MFStartup(uint version, uint flags);
    [DllImport("mfplat.dll")] private static extern int MFShutdown();
    [DllImport("mfplat.dll")] private static extern int MFCreateAttributes(out nint attributes, uint initialSize);
    [DllImport("mfplat.dll")] private static extern int MFCreateMediaType(out nint type);
    [DllImport("mfplat.dll")] private static extern int MFCreateMemoryBuffer(uint length, out nint buffer);
    [DllImport("mfplat.dll")] private static extern int MFCreateSample(out nint sample);
    [DllImport("mfreadwrite.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern int MFCreateSinkWriterFromURL(string path, nint stream, nint attributes, out nint writer);
}
