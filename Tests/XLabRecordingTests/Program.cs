using System.Runtime.InteropServices;
using XLab.Shared.Recording;
using Xmax.SDK;

static class Program
{
    private static int passed;
    internal static void Check(bool condition, string message)
    { if (!condition) throw new InvalidOperationException(message); }
    private static async Task Test(string name, Func<Task> test)
    { await test().WaitAsync(TimeSpan.FromSeconds(40)); Console.WriteLine("PASS " + name); ++passed; }
    static async Task Main(string[] args)
    {
        await Test("empty recording and exactly-once ownership", async () =>
        {
            var writer = new FakeWriter(); await using var recorder = new RemoteMediaRecorder(() => { }, () => writer);
            var packet = Packet.Audio(0); recorder.Append(packet);
            var first = recorder.FinishAsync(); Check(ReferenceEquals(first, recorder.FinishAsync()), "Finish must be coalesced.");
            var result = await first;
            Check(result.Path is null && result.Error is not null && !writer.Opened && packet.Disposals == 1, "Empty recording leaked a frame or returned an invalid clip.");
            var late = Packet.Video(0); recorder.Append(late); Check(late.Disposals == 1, "Late callback was not released.");
        });
        await Test("common clock, pre-roll trim, overlap and stereo resampling", async () =>
        {
            var writer = new FakeWriter(); await using var recorder = new RemoteMediaRecorder(() => { }, () => writer);
            var prefix = Packet.Audio(990_000, 44100, 2, 882); var video = Packet.Video(1_000_000);
            recorder.Append(prefix); recorder.Append(video); recorder.Append(Packet.Audio(1_000_000, 44100, 2, 882));
            var result = await recorder.FinishAsync(); Check(result.Error is null, result.Error ?? "");
            Check(writer.VideoTimes.SequenceEqual(new long[] { 0 }), "Video origin was not zero.");
            Check(writer.AudioTimes.Count == 2 && writer.AudioTimes[0] == 0 && writer.AudioTimes[1] >= 99_000, "Pre-roll/overlap trimming lost the common clock.");
            Check(writer.AudioSizes.All(size => size is >= 950 and <= 962), "44.1 kHz stereo was not normalized to 48 kHz mono.");
            Check(writer.Peak > 4000 && prefix.Disposals == 1 && video.Disposals == 1, "Audio was muted or input ownership was lost.");
        });
        await Test("late audio preserves its video-relative offset", async () =>
        {
            var writer = new FakeWriter(); await using var recorder = new RemoteMediaRecorder(() => { }, () => writer);
            recorder.Append(Packet.Video(1_000_000)); recorder.Append(Packet.Audio(1_250_000));
            recorder.Append(Packet.Audio(1_750_000));
            Check((await recorder.FinishAsync()).Error is null, "Late audio failed.");
            Check(writer.AudioTimes.SequenceEqual(new long[] { 2_500_000, 7_500_000 }), "Audio timestamps were reset per track.");
        });
        await Test("duplicate frames and backwards clock preserve a finalized prefix", async () =>
        {
            var writer = new FakeWriter(); int interrupted = 0;
            await using var recorder = new RemoteMediaRecorder(() => Interlocked.Increment(ref interrupted), () => writer);
            var frames = new[] { Packet.Video(1_000_000), Packet.Video(1_000_000), Packet.Video(1_040_000), Packet.Video(0) };
            foreach (var packet in frames) recorder.Append(packet);
            var result = await recorder.FinishAsync();
            Check(result.Path is not null && result.Error is not null && writer.Finished && interrupted == 1, "Clock restart did not finalize the usable prefix.");
            Check(writer.VideoTimes.SequenceEqual(new long[] { 0, 400_000 }) && frames.All(p => p.Disposals == 1), "Duplicate frames or ownership handling failed.");
        });
        await Test("stream and audio format changes interrupt instead of mixing", async () =>
        {
            foreach (bool changeStream in new[] { false, true })
            {
                var writer = new FakeWriter(); await using var recorder = new RemoteMediaRecorder(() => { }, () => writer);
                recorder.Append(Packet.Video(0)); recorder.Append(Packet.Audio(0));
                var changed = changeStream ? Packet.Video(40_000) : Packet.Audio(20_000, 44100);
                if (changeStream) changed.StreamId = 2;
                recorder.Append(changed);
                var result = await recorder.FinishAsync();
                Check(result.Path is not null && result.Error is not null && writer.Finished && changed.Disposals == 1, "A changed stream/format was silently mixed.");
            }
        });
        await Test("queue includes in-flight bytes and overflow preserves accepted frames", async () =>
        {
            var writer = new FakeWriter { Block = true }; int interrupted = 0;
            await using var recorder = new RemoteMediaRecorder(() => ++interrupted, () => writer, 2, 96 * 64 * 4 * 2);
            var first = Packet.Video(0); var queued = Packet.Video(40_000); var rejected = Packet.Video(80_000);
            recorder.Append(first); Check(writer.Entered.Wait(5000), "Encoder did not start.");
            recorder.Append(queued); recorder.Append(rejected);
            writer.Release.Set(); var result = await recorder.FinishAsync();
            Check(interrupted == 1 && result.Path is not null && result.Error is not null && writer.VideoTimes.Count == 2, "Overflow did not preserve the accepted prefix.");
            Check(new[] { first, queued, rejected }.All(p => p.Disposals == 1), "Overflow leaked or double-freed a packet.");
        });
        await Test("encoding failure finalizes prefix and disposes queued packets", async () =>
        {
            var writer = new FakeWriter { ThrowAt = 2 }; await using var recorder = new RemoteMediaRecorder(() => { }, () => writer);
            var frames = Enumerable.Range(0, 10).Select(i => Packet.Video(i * 40_000)).ToArray();
            foreach (var packet in frames) recorder.Append(packet);
            var result = await recorder.FinishAsync();
            Check(result.Path is not null && result.Error is not null && writer.Finished && frames.All(p => p.Disposals == 1), "Encoder failure lost its usable prefix or leaked packets.");
        });
        await Test("finalization failure never returns an unusable MP4", async () =>
        {
            var writer = new FakeWriter { FailFinish = true }; await using var recorder = new RemoteMediaRecorder(() => { }, () => writer);
            recorder.Append(Packet.Video(0)); var result = await recorder.FinishAsync();
            Check(result.Path is null && result.Error is not null && writer.Disposed, "Failed finalization exposed an incomplete clip.");
        });
        await Test("save retry, atomic replace and explicit discard", async () =>
        {
            var writer = new FakeWriter(); var recorder = new RemoteMediaRecorder(() => { }, () => writer);
            recorder.Append(Packet.Video(0)); string source = (await recorder.FinishAsync()).Path!;
            string directory = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")); Directory.CreateDirectory(directory);
            try
            {
                string destination = Path.Combine(directory, "clip.mp4"); await File.WriteAllTextAsync(destination, "existing");
                using (var locked = new FileStream(destination, FileMode.Open, FileAccess.Read, FileShare.None))
                {
                    try { await RemoteMediaRecorder.SaveAsync(source, destination); throw new Exception("Expected save failure."); } catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
                    Check(File.Exists(source), "Failed save discarded the recording.");
                }
                Check(await File.ReadAllTextAsync(destination) == "existing", "Failed save overwrote the existing destination.");
                await RemoteMediaRecorder.SaveAsync(source, destination);
                Check(await File.ReadAllTextAsync(destination) == "finalized" && Directory.GetFiles(directory).Length == 1, "Retry or staging cleanup failed.");
                await recorder.DisposeAsync(); Check(!File.Exists(source), "Discard did not remove the temporary recording.");
                Check(File.Exists(destination), "Discard removed the saved clip.");
            }
            finally { await recorder.DisposeAsync(); Directory.Delete(directory, true); }
        });
        if (args.Contains("--native"))
        {
            string artifacts = args.FirstOrDefault(a => a.StartsWith("--artifacts="))?[12..] ?? Path.Combine(Path.GetTempPath(), "XLab-recording-tests");
            Directory.CreateDirectory(artifacts);
            await Test("Media Foundation H.264/AAC shared clock and changing canvas", async () =>
            {
                await using var recorder = new RemoteMediaRecorder(() => { });
                for (int n = 0; n < 48; ++n)
                {
                    long time = 1_000_000 + n * 41666;
                    recorder.Append(Packet.Video(time, n < 24 ? 96 : 48, 64));
                    if (n >= 6 && n < 42)
                        foreach (int offset in new[] { 0, 10000, 20000 }) recorder.Append(Packet.Audio(time + offset));
                    await Task.Delay(15);
                }
                var result = await recorder.FinishAsync(); Check(result.Error is null && result.Path is not null, result.Error ?? "No MP4.");
                await RemoteMediaRecorder.SaveAsync(result.Path!, Path.Combine(artifacts, "remote-av.mp4"));
                MediaFoundationProbe.Verify(Path.Combine(artifacts, "remote-av.mp4"), 96, 64, true);
            });
            await Test("1080p recording keeps up with paced 24 fps remote input", async () =>
            {
                await using var recorder = new RemoteMediaRecorder(() => { });
                var packets = new List<Packet>();
                for (int n = 0; n < 48; ++n)
                {
                    var frame = Packet.Video(n * 41666L, 1920, 1080); packets.Add(frame); recorder.Append(frame);
                    foreach (int offset in new[] { 0, 10000, 20000, 30000 }) recorder.Append(Packet.Audio(n * 41666L + offset));
                    await Task.Delay(42);
                }
                var result = await recorder.FinishAsync();
                Check(result.Path is not null && result.Error is null && packets.All(packet => packet.Disposals == 1), result.Error ?? "The paced recording failed.");
                await RemoteMediaRecorder.SaveAsync(result.Path!, Path.Combine(artifacts, "paced-1080p.mp4"));
            });
            foreach (var (width, height) in new[] { (1920, 1080), (1080, 1920) })
                await Test($"Media Foundation video-only {width}x{height}", async () =>
                {
                    await using var recorder = new RemoteMediaRecorder(() => { });
                    recorder.Append(Packet.Video(0, width, height)); recorder.Append(Packet.Video(1_000_000, width, height));
                    var result = await recorder.FinishAsync(); Check(result.Error is null && result.Path is not null, result.Error ?? "No MP4.");
                    await RemoteMediaRecorder.SaveAsync(result.Path!, Path.Combine(artifacts, $"video-{width}x{height}.mp4"));
                    MediaFoundationProbe.Verify(Path.Combine(artifacts, $"video-{width}x{height}.mp4"), width, height, false);
                });
        }
        Console.WriteLine($"{passed} recording tests passed.");
    }
}

internal sealed class FakeWriter : IRecordingWriter
{
    internal readonly List<long> VideoTimes = [], AudioTimes = [];
    internal readonly List<int> AudioSizes = [];
    internal bool Opened, Finished, Disposed, Block, FailFinish;
    internal int ThrowAt = int.MaxValue, Peak;
    internal readonly ManualResetEventSlim Entered = new(), Release = new();
    private string path = "";
    public void Open(string path, int width, int height) { this.path = path; Opened = true; File.WriteAllText(path, "open"); }
    public void Video(nint pixels, int stride, int size, int width, int height, long time, long duration)
    {
        Entered.Set(); if (Block && !Release.Wait(5000)) throw new TimeoutException();
        if (VideoTimes.Count + 1 == ThrowAt) throw new IOException("Injected encoder failure.");
        VideoTimes.Add(time);
    }
    public void Audio(ReadOnlySpan<byte> pcm, long time)
    {
        AudioTimes.Add(time); AudioSizes.Add(pcm.Length);
        foreach (short sample in MemoryMarshal.Cast<byte, short>(pcm)) Peak = Math.Max(Peak, Math.Abs((int)sample));
    }
    public void Finish() { if (FailFinish) throw new IOException("Injected finalization failure."); Finished = true; File.WriteAllText(path, "finalized"); }
    public void Dispose() { Disposed = true; Entered.Dispose(); Release.Dispose(); }
}

internal sealed class Packet : IRecordingPacket
{
    public MediaType Type { get; init; }
    public ulong StreamId { get; set; } = 1;
    public MediaTime Time { get; init; }
    public MediaTime Duration { get; init; }
    public int Width { get; init; }
    public int Height { get; init; }
    public int SampleRate { get; init; }
    public int Channels { get; init; }
    public int Samples { get; init; }
    internal int Disposals;
    private byte[] bytes = [];
    public int ByteCount => bytes.Length;
    internal static Packet Video(long microseconds, int width = 96, int height = 64)
    {
        var result = new Packet { Type = MediaType.Video, Time = new(microseconds, 1_000_000), Duration = new(41666, 1_000_000), Width = width, Height = height, bytes = new byte[width * height * 4] };
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        { int offset = (y * width + x) * 4; result.bytes[offset + (y < height / 2 ? 2 : 0)] = 255; result.bytes[offset + 3] = 255; }
        return result;
    }
    internal static Packet Audio(long microseconds, int rate = 48000, int channels = 1, int samples = 480)
    {
        var result = new Packet { Type = MediaType.Audio, Time = new(microseconds, 1_000_000), Duration = new(samples, rate), SampleRate = rate, Channels = channels, Samples = samples, bytes = new byte[samples * channels * 2] };
        var pcm = MemoryMarshal.Cast<byte, short>(result.bytes.AsSpan());
        for (int n = 0; n < samples; ++n) for (int c = 0; c < channels; ++c) pcm[n * channels + c] = (short)(8000 * Math.Sin(2 * Math.PI * 440 * n / rate));
        return result;
    }
    public unsafe void ReadVideo(Action<nint, int, int> read) { fixed (byte* data = bytes) read((nint)data, Width * 4, bytes.Length); }
    public unsafe void ReadAudio(Action<nint, int> read) { fixed (byte* data = bytes) read((nint)data, bytes.Length); }
    public void Dispose() => Interlocked.Increment(ref Disposals);
}
