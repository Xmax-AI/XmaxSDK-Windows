//
//  Program.cs
//  XmaxSDK
//
//  Author: 翁跃庭 (Yueting Weng)
//  GitHub: https://github.com/Xmax-AI/XmaxSDK-Windows
//
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using Xmax.SDK;
using Xmax.SDK.Interop;
using Xmax.SDK.Rendering;
using Xmax.SDK.Runtime;

static void Check(bool value, string message) { if (!value) throw new Exception(message); }
static async Task ThrowsAsync<T>(Func<Task> body) where T : Exception
{ try { await body(); } catch (T) { return; } throw new Exception($"Expected {typeof(T).Name}"); }
static unsafe NativeHandle CreateNativeClient()
{
    var options = new xmax_client_options { struct_size = (uint)sizeof(xmax_client_options), abi_version = 2 };
    nint value = 0;
    NativeError.Check(NativeMethods.xmax_client_create_with_options(&options, &value));
    return new(value, HandleKind.Client);
}

int passed = 0;
async Task Test(string name, Func<Task> body)
{ await body(); ++passed; Console.WriteLine($"PASS {name}"); }

try
{
    if (args.Length > 0)
        await Test("C and C# layouts and event values match the public ABI", () =>
        {
            foreach (var line in File.ReadAllLines(args[0]))
            {
                string[] items = line.Split(' ');
                var type = typeof(XmaxClient).Assembly.GetType((items[0] == "enum" ? "Xmax.SDK." : "Xmax.SDK.Interop.") + items[1])!;
                long actual = items[0] == "enum" ? Convert.ToInt64(Enum.Parse(type, items[2])) : items[0] == "sizeof" ? Marshal.SizeOf(type) : Marshal.OffsetOf(type, items[2]).ToInt64();
                Check(actual == long.Parse(items[^1]), $"ABI mismatch: {line}, managed {actual}");
            }
            return Task.CompletedTask;
        });
    await Test("trajectory pointer ends immediately and the trail fades independently", () =>
    {
        var effect = new DefaultTrajectoryEffect();
        long started = Stopwatch.GetTimestamp();
        var first = new TrajectoryPoint(20, 20);
        var last = new TrajectoryPoint(80, 60);
        effect.Sample(first, started);
        Check(effect.Head?.Center == first && effect.Strokes.Count == 0, "stationary pointer has a head but no trail");
        effect.Sample(last, started);
        var stroke = effect.Strokes.Single();
        Check(stroke.From == first && stroke.To == last && effect.Head?.Center == last, "move joins the displayed coordinates");
        effect.End();
        long halfway = started + Stopwatch.Frequency / 2;
        Check(effect.Head is null && effect.Advance(halfway), "release removes the pointer and retains the trail");
        Check(stroke.Opacity(halfway) is > .2 and < .3, "trail fades exponentially");
        effect.Advance(started + 2 * Stopwatch.Frequency);
        Check(effect.Strokes.Count == 0, "expired trail clears");
        effect.Sample(first, halfway);
        effect.Reset();
        Check(effect.Head is null && effect.Strokes.Count == 0, "reset clears all effects");
        return Task.CompletedTask;
    });
    await Test("model capabilities, client ownership and ABI flags", async () =>
    {
        Check(XmaxRuntime.Version.Length > 0, "version");
        var model = XmaxClient.GetModelInfo();
        Check(model.Identifier == "x2.1", "default model");
        Check(model.DisplayName == "X2.1" && model.DefaultCameraFormat == new VideoFormat(1920, 1080), "X2.1 defaults to 1920×1080 capture");
        Check(model.SupportedFormats.SequenceEqual(new[] { new VideoFormat(1920,1080), new VideoFormat(1080,1920),
            new VideoFormat(1408,1408), new VideoFormat(1152,1536), new VideoFormat(1536,1152) }), "five standard formats");
        await ThrowsAsync<ArgumentOutOfRangeException>(() =>
        {
            XmaxClient.GetModelInfo((RealtimeModel)999);
            return Task.CompletedTask;
        });
        using (var owner = new XmaxClient())
        {
            await using var selected = owner.CreateRealtime(new() { Model = RealtimeModel.X2_1 });
            await selected.PrepareExternalAsync(new(16, 8));
        }
        var client = new XmaxClient();
        Check(client.GetCapabilities().HasFlag(XmaxCapabilities.Storage), "storage capability");
        var realtime = client.CreateRealtime();
        client.Dispose();
        await using (realtime)
        {
            await realtime.PrepareExternalAsync(new(16, 8));
            Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Ready, "independent child");
        }
    });
    await Test("beauty configuration uses the public SDK and validates before native changes", async () =>
    {
        Check(new RealtimeVideoBeautyOptions() == new RealtimeVideoBeautyOptions(0, 0, 0, 0), "All default beauty strengths are zero");
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        await realtime.SetRemoteVideoBeautyErrorListenerAsync(_ => throw new Exception("Configuration errors must not use the frame-error listener"));
        await realtime.SetRemoteVideoBeautyAsync(null);
        foreach (float invalid in new[] { -.1f, 1.1f, float.NaN, float.PositiveInfinity })
            await ThrowsAsync<XmaxException>(() => realtime.SetRemoteVideoBeautyAsync(new(Smoothness: invalid)));
        string runtime = Environment.GetEnvironmentVariable("XMAX_NATIVE_DIR") ?? AppContext.BaseDirectory;
        if (OperatingSystem.IsWindows() && File.Exists(Path.Combine(runtime, "Xmax.VideoBeautyWorker.exe"))) {
            await realtime.SetRemoteVideoBeautyAsync(new());
            await realtime.SetRemoteVideoBeautyAsync(new(Smoothness: .4f, Lightening: .3f));
            await realtime.SetRemoteVideoBeautyAsync(null);
        }
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Idle, "Beauty does not create a session or capture input");
        await realtime.CloseAsync();
        await realtime.SetRemoteVideoBeautyErrorListenerAsync(null);
    });
    await Test("typed credit queries fail explicitly and honor cancellation without starting a session", async () =>
    {
        using var client = new XmaxClient(new() { ApiKey = " \t \n " });
        try { await client.GetCreditBalanceAsync(); throw new Exception("Invalid key became a balance"); }
        catch (XmaxException error) { Check(error.Result == -14, "Expected missing API-key error"); }
        using var cancellation = new CancellationTokenSource();
        // Exercise a live cancellation callback across the managed/native ABI without a network request.
        await ThrowsAsync<XmaxException>(() => client.GetCreditBalanceAsync(cancellation.Token));
        cancellation.Cancel();
        await ThrowsAsync<OperationCanceledException>(() => client.GetCreditBalanceAsync(cancellation.Token));
        var disposed = new XmaxClient(); disposed.Dispose();
        await ThrowsAsync<ObjectDisposedException>(() => disposed.GetCreditBalanceAsync());
    });
    await Test("UTF-8 errors and async thread-local diagnostics", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        try { await realtime.PrepareExternalAsync(new(3, 5)); throw new Exception("Expected invalid argument"); }
        catch (XmaxException error) { Check(error.Result == -1 && error.Message.Length > 0, "structured error"); }
        var stream = await realtime.CreateExternalVideoStreamAsync(new ExternalTestSource());
        await ThrowsAsync<XmaxException>(() => realtime.StartGenerationAsync(stream, new("中文提示词 👋")));
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Ready, "failed generation retains input");
    });
    await Test("strided BGRA ownership, mirror metadata and retained frames", async () =>
    {
        using var client = new XmaxClient();
        var realtime = client.CreateRealtime();
        using var sink = await realtime.BindExternalAsync(new(16, 8));
        await realtime.SetVideoMirrorAsync(new(true, false, false));
        byte[] pixels = Enumerable.Repeat((byte)127, 80 * 8).ToArray();
        sink.PushBgra(pixels, 80, 1000);
        using var frame = realtime.LocalVideo.TryAcquireFrame() ?? throw new Exception("No local frame");
        Array.Fill(pixels, (byte)0);
        Check(frame.Width == 16 && frame.Height == 8 && frame.Mirrored, "frame metadata");
        Check(frame.CopyBgra()[0] == 127, "push must copy pixels");
        await realtime.DisposeAsync();
        Check(frame.CopyBgra()[0] == 127, "frame outlives realtime");
        Check(!sink.IsRequestingFrames, "source becomes inert after destruction");
        sink.PushBgra(pixels, 80, 2000);
    });
    await Test("external stream starts without a frame and resizes original source pixels", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        var source = new ExternalTestSource(framesPerSecond: 90);
        var stream = await realtime.CreateExternalVideoStreamAsync(source, new(640, 480, 24));
        var sink = source.Sink ?? throw new Exception("Source did not receive a sink");
        Check(sink.Format == new VideoFormat(16, 8, 90) && sink.VideoFormat == new VideoFormat(640, 480, 24),
            "capture and processing formats");
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Ready, "external stream is ready without a frame");
        Check(stream.VideoTrack.TryAcquireFrame() is null, "no frame before the source pushes one");
        await ThrowsAsync<XmaxException>(() => realtime.CreateExternalVideoStreamAsync(new ExternalTestSource()));

        sink.PushBgra(Enumerable.Repeat((byte)127, 64 * 8).ToArray(), 64, 1000);
        using (var frame = stream.VideoTrack.TryAcquireFrame() ?? throw new Exception("No resized preview"))
            Check(frame.Width == 640 && frame.Height == 480 && frame.CopyBgra()[0] == 127, "resized BGRA");
        sink.PushNv12(new byte[16 * 8], 16, Enumerable.Repeat((byte)45, 16 * 4).ToArray(), 16, 2000);
        using (var frame = stream.VideoTrack.TryAcquireFrame() ?? throw new Exception("No resized NV12 preview"))
            frame.ReadPlane(1, (pointer, stride, _) =>
                Check(Marshal.ReadByte(pointer) == 45 && stride == 640, "resized NV12 chroma"));
        sink.PushI420(new byte[16 * 8], 16, new byte[8 * 4], 8, Enumerable.Repeat((byte)67, 8 * 4).ToArray(), 8, 3000);
        using (var frame = stream.VideoTrack.TryAcquireFrame() ?? throw new Exception("No normalized preview"))
        {
            Check(frame.PixelFormat == VideoPixelFormat.Nv12, "typed source normalizes I420 to NV12");
            frame.ReadPlane(1, (pointer, stride, _) =>
                Check(Marshal.ReadByte(pointer) == 0 && Marshal.ReadByte(pointer, 1) == 67 && stride == 640, "interleaved chroma"));
        }

        await realtime.DisconnectAsync();
        Check(sink.IsRequestingFrames && source.StopCount == 0, "disconnect retains preview");
        await realtime.StopExternalVideoStreamAsync();
        Check(source.StopCount == 1 && stream.VideoTrack.TryAcquireFrame() is null, "stop detaches preview");
        var replacement = new ExternalTestSource();
        var next = await realtime.CreateExternalVideoStreamAsync(replacement);
        replacement.Sink!.PushBgra(new byte[64 * 8], 64, 1000);
        Check(stream.VideoTrack.TryAcquireFrame() is null, "old track does not consume replacement frames");
        using var current = next.VideoTrack.TryAcquireFrame();
        Check(current is not null && current.Width % 4 == 0 && current.Height % 4 == 0 &&
            current.Width * current.Height >= XmaxClient.GetModelInfo().MinimumInputPixels, "normalized default format");
        await realtime.CloseAsync();
        Check(replacement.StopCount == 1, "close stops the replacement source");
    });
    await Test("closing cancels source startup and allows a clean retry", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        var source = new ExternalTestSource(blockStartup: true);
        var creating = realtime.CreateExternalVideoStreamAsync(source);
        await source.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Preparing,
            "ready is published only after the source has installed delivery");
        await realtime.DisconnectAsync();
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Preparing,
            "disconnect does not publish ready during source startup");
        await realtime.CloseAsync().WaitAsync(TimeSpan.FromSeconds(5));
        await ThrowsAsync<OperationCanceledException>(() => creating);
        Check(source.StopCount == 1 && (await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Idle,
            "cancelled startup released its attachment");
        var replacement = new ExternalTestSource();
        var stream = await realtime.CreateExternalVideoStreamAsync(replacement);
        replacement.Sink!.ReportError(-11, "external capture failed");
        await replacement.Stopped.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Idle &&
            stream.VideoTrack.TryAcquireFrame() is null, "source failure releases media");
        var finalSource = new ExternalTestSource();
        await realtime.CreateExternalVideoStreamAsync(finalSource);
        await realtime.DisposeAsync();
        Check(finalSource.StopCount == 1, "disposal stops source delivery");
    });
    await Test("stream ownership, downlink sizing and typed listener registration", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        await using var other = client.CreateRealtime();
        var media = client.CreateMediaService();
        var target = media.ResolveTargetSize(new(1920, 1080), 1920, 1080);
        Check(target == new VideoFormat(1920, 1080), "downlink preserves the dimensions within tier limits");
        _ = await realtime.ListMicrophoneDevicesAsync();
        Check(!await realtime.GetMicrophoneEnabledAsync(), "no camera microphone is active");

        var initialLevel = new TaskCompletionSource<float?>(TaskCreationOptions.RunContinuationsAsynchronously);
        await realtime.SetMicrophoneLevelListenerAsync(level => initialLevel.TrySetResult(level));
        Check(initialLevel.Task.IsCompletedSuccessfully && initialLevel.Task.Result is null, "registration awaits the initial microphone reading");
        await realtime.SetMicrophoneLevelListenerAsync(null);
        var state = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        await realtime.SetStateListenerAsync(item =>
        {
            if (item.ConnectionState == RealtimeConnectionState.Ready)
                state.TrySetResult();
        });
        var encoding = new VideoFormat(1280, 720, 30, 600, 2500, EncodingPreference.MaintainQuality);
        var original = await realtime.CreateExternalVideoStreamAsync(new ExternalTestSource(), encoding);
        var trackFormat = original.VideoTrack.VideoFormat;
        Check(original.VideoTrack.Id == original.Id && original.VideoTrack.Device is null &&
            trackFormat?.MinimumBitrateKbps == 600 && trackFormat?.MaximumBitrateKbps == 2500 &&
            trackFormat?.EncoderPreference == EncodingPreference.MaintainQuality, "track carries applied external encoding");
        await state.Task.WaitAsync(TimeSpan.FromSeconds(3));
        await ThrowsAsync<ArgumentException>(() => other.ConnectAsync(original));
        await ThrowsAsync<XmaxException>(() => realtime.ConnectAsync(original));
        try
        {
            await realtime.StartGenerationAsync(new GenerationContext("prompt"));
            throw new Exception("Context-only generation connected unexpectedly");
        }
        catch (XmaxException error) when (error.Result == -2) { }
        await realtime.StopLocalCameraStreamAsync();
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Ready, "camera stop preserves external input");
        var disconnected = await realtime.GetStateAsync();
        Check(disconnected.Reason is RealtimeReason.Failure { Error.Result: -14 }, "connection failure is published");
        await realtime.DisconnectAsync(RealtimeReason.OrientationChanged);
        await realtime.DisconnectAsync(new RealtimeReason.Failure(new XmaxException(-4, "调用方断开", 42, 403)));
        var repeated = await realtime.GetStateAsync();
        Check(repeated.ConnectionState == RealtimeConnectionState.Ready && repeated.SessionId == disconnected.SessionId &&
            repeated.Reason is RealtimeReason.Failure { Error.Result: -14 }, "disconnect on ready preserves the native state and reason");
        await realtime.StopExternalVideoStreamAsync();
        var replacement = await realtime.CreateExternalVideoStreamAsync(new ExternalTestSource());
        Check(replacement.Id != original.Id, "replacement receives a new stream identity");
        await ThrowsAsync<XmaxException>(() => realtime.StartGenerationAsync(original, new("prompt")));
        using var storageClient = new XmaxClient(new() { ApiKey = "offline-test" });
        await using var storage = storageClient.CreateStorage();
        await ThrowsAsync<XmaxException>(() => storage.UploadImageAsync(new byte[] { 1 }, "wrong.mp4"));
        await ThrowsAsync<XmaxException>(() => storage.UploadVideoAsync(new byte[] { 1 }, "wrong.png"));
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Ready, "stale generation does not disturb the current input");
    });
    await Test("typed state carries failure details to the chosen callback context", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        var initial = await realtime.GetStateAsync();
        Check(initial == new RealtimeState(RealtimeConnectionState.Idle), "initial identifiers and reason are absent");
        var context = new TestCallbackContext();
        var delivered = new TaskCompletionSource<RealtimeState>(TaskCreationOptions.RunContinuationsAsynchronously);
        await realtime.SetStateListenerAsync(state =>
        {
            if (state.Reason is RealtimeReason.Failure && SynchronizationContext.Current == context)
                delivered.TrySetResult(state);
        }, context);
        var source = new ExternalTestSource();
        await realtime.CreateExternalVideoStreamAsync(source);
        source.Sink!.ReportError(-11, "External capture lost");
        var failure = await delivered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Check(failure.ConnectionState == RealtimeConnectionState.Idle &&
            failure.Reason is RealtimeReason.Failure { Error.Result: -11, Error.Message: "External capture lost" },
            "state callback includes the original failure");
        Check((await realtime.GetStateAsync()).Reason is RealtimeReason.Failure { Error.Message: "External capture lost" },
            "state query includes the original failure");
        var replay = new TaskCompletionSource<RealtimeState>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var finishReplay = new ManualResetEventSlim();
        var registration = realtime.SetStateListenerAsync(state =>
        {
            replay.TrySetResult(state);
            finishReplay.Wait(TimeSpan.FromSeconds(5));
        }, context);
        Check((await replay.Task.WaitAsync(TimeSpan.FromSeconds(5))).Reason is
            RealtimeReason.Failure { Error.Message: "External capture lost" }, "registration replays the current state");
        Check(!registration.IsCompleted, "registration waits for the replay callback to return");
        finishReplay.Set();
        await registration.WaitAsync(TimeSpan.FromSeconds(5));
        Check(new GenerationContext("  prompt  ", " \u200B\u3000").ReferencePath is null &&
            new GenerationContext("\u200B提示词\u2029", "  ref ").Prompt == "提示词", "generation context normalization");
    });
    await Test("source replacement discards stale frames", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        using var old = await realtime.BindExternalAsync(new(16, 8));
        using var current = await realtime.BindExternalAsync(new(16, 8));
        byte[] pixels = new byte[64 * 8];
        old.PushBgra(pixels, 64, 1000);
        Check(realtime.LocalVideo.TryAcquireFrame() is null, "stale binding ignored");
        current.PushBgra(pixels, 64, 2000);
        using var frame = realtime.LocalVideo.TryAcquireFrame();
        Check(frame?.TimestampMicroseconds == 2000, "current binding accepted");
    });
    await Test("NV12 and I420 preserve plane layout through the managed boundary", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        using var sink = await realtime.BindExternalAsync(new(16, 8));
        sink.PushNv12(new byte[20 * 8], 20, Enumerable.Repeat((byte)45, 20 * 4).ToArray(), 20, 1000);
        using (var frame = realtime.LocalVideo.TryAcquireFrame()!)
        {
            Check(frame.PixelFormat == VideoPixelFormat.Nv12, "NV12 format");
            frame.ReadPlane(1, (pointer, stride, size) => Check(Marshal.ReadByte(pointer) == 45 && stride >= 16 && size >= 64, "NV12 chroma plane"));
        }
        sink.PushI420(new byte[20 * 8], 20, new byte[10 * 4], 10, Enumerable.Repeat((byte)67, 10 * 4).ToArray(), 10, 2000);
        using (var frame = realtime.LocalVideo.TryAcquireFrame()!)
        {
            Check(frame.PixelFormat == VideoPixelFormat.I420, "I420 format");
            frame.ReadPlane(2, (pointer, stride, size) => Check(Marshal.ReadByte(pointer) == 67 && stride >= 8 && size >= 32, "I420 V plane"));
        }
    });
    await Test("event callback failures are isolated", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        var delivered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        realtime.EventReceived += _ => throw new InvalidOperationException("Application callback failure");
        realtime.CallbackFaulted += _ => delivered.TrySetResult();
        await realtime.PrepareExternalAsync(new(16, 8));
        await delivered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Check((await realtime.GetStateAsync()).ConnectionState == RealtimeConnectionState.Ready, "callback must not break lifecycle");
    });
    await Test("cancelled admission and repeated asynchronous disposal", async () =>
    {
        using var client = new XmaxClient();
        var realtime = client.CreateRealtime();
        using var stop = new CancellationTokenSource(); stop.Cancel();
        var stream = await realtime.CreateExternalVideoStreamAsync(new ExternalTestSource());
        await ThrowsAsync<OperationCanceledException>(() => realtime.ConnectAsync(stream, stop.Token));
        await Task.WhenAll(realtime.DisposeAsync().AsTask(), realtime.DisposeAsync().AsTask());
        await ThrowsAsync<ObjectDisposedException>(() => realtime.PrepareExternalAsync(new(16, 8)));
    });
    await Test("cancellation can interrupt blocking native work without an admission deadlock", async () =>
    {
        using var stop = new CancellationTokenSource();
        using var started = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var task = Task.Run(() => NativeCancellation.Run(stop.Token, () =>
        { started.Set(); Check(release.Wait(5000), "cancel did not run concurrently"); return 1; }, () => release.Set()));
        Check(started.Wait(5000), "operation did not begin");
        await stop.CancelAsync();
        await ThrowsAsync<OperationCanceledException>(async () => await task);
    });
    await Test("cancellation also reaches an operation admitted after its first cancel request", async () =>
    {
        using var stop = new CancellationTokenSource();
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        int ready = 0;
        var task = Task.Run(() => NativeCancellation.Run(stop.Token, () =>
        {
            entered.Set(); Thread.Sleep(60); Volatile.Write(ref ready, 1);
            Check(release.Wait(3000), "early cancellation missed late native admission");
            return 1;
        }, () => { if (Volatile.Read(ref ready) == 1) release.Set(); }));
        Check(entered.Wait(3000), "operation did not enter");
        await stop.CancelAsync();
        await ThrowsAsync<OperationCanceledException>(async () => await task);
    });
    await Test("storage cancellation, error propagation and disposal", async () =>
    {
        foreach (string key in new[] { "", "\u200B\u3000" })
        {
            using var missingKey = new XmaxClient(new() { ApiKey = key });
            try { await using var unexpected = missingKey.CreateStorage(); throw new Exception("Expected an empty API key error"); }
            catch (XmaxException error) { Check(error.Result == -14, "storage validates the API key at creation"); }
        }
        using var client = new XmaxClient(new() { ApiKey = "offline-test" });
        await using var storage = client.CreateStorage();
        Check(storage.GetProgress().State == 0, "initial progress");
        await ThrowsAsync<XmaxException>(() => storage.UploadFileAsync("missing-中文.png"));
        storage.Cancel();
    });
    await Test("typed storage downloads follow a loopback HTTP redirect", async () =>
    {
        using var client = new XmaxClient(new() { ApiKey = "offline-test" });
        await using var storage = client.CreateStorage();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        async Task ServeAsync()
        {
            foreach (var path in new[] { "/redirect", "/image" })
            {
                using var connection = await listener.AcceptTcpClientAsync(timeout.Token);
                using var network = connection.GetStream();
                using var reader = new StreamReader(network, leaveOpen: true);
                var request = await reader.ReadLineAsync(timeout.Token);
                Check(request?.StartsWith($"GET {path} ", StringComparison.Ordinal) == true, "expected redirect request");
                while (await reader.ReadLineAsync(timeout.Token) is { Length: > 0 }) { }
                string response = path == "/redirect"
                    ? "HTTP/1.1 302 Found\r\nLocation: /image\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                    : "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\ndata";
                await network.WriteAsync(Encoding.ASCII.GetBytes(response), timeout.Token);
            }
        }

        string destination = Path.Combine(Path.GetTempPath(), $"xmax-redirect-{Guid.NewGuid():N}.png");
        try
        {
            var serving = ServeAsync();
            var downloading = storage.DownloadImageAsync($"http://127.0.0.1:{port}/redirect", destination,
                cancellationToken: timeout.Token);
            await Task.WhenAll(serving, downloading);
            Check(File.ReadAllText(destination) == "data", "only the redirected file body was published");
        }
        finally { File.Delete(destination); }
    });
    await Test("disposal cancels late admission and retains the native handle until completion", async () =>
    {
        using var handle = CreateNativeClient();
        var operations = new OperationTracker(handle);
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        int ready = 0;
        var task = operations.Run(value =>
        {
            entered.Set(); Thread.Sleep(60); Volatile.Write(ref ready, 1);
            Check(release.Wait(3000), "close missed late admission");
            Check(!handle.IsClosed, "native handle released while in use");
            return 0;
        });
        Check(entered.Wait(3000), "operation did not start");
        await operations.DisposeAsync(_ => { if (Volatile.Read(ref ready) == 1) release.Set(); }).WaitAsync(TimeSpan.FromSeconds(5));
        await task;
        Check(handle.IsClosed, "native handle was not released");
    });
    await Test("remote callback registration survives GC and supports replacement", async () =>
    {
        using var client = new XmaxClient();
        await using var realtime = client.CreateRealtime();
        await realtime.SetRemoteMediaListenerAsync(packet => packet.Dispose());
        GC.Collect(); GC.WaitForPendingFinalizers();
        await realtime.SetRemoteMediaListenerAsync(packet => packet.Dispose());
        await realtime.SetRemoteMediaListenerAsync(null);
        Check(realtime.GetUplinkVideoStatistics() is null && realtime.GetDownlinkVideoStatistics() is null, "statistics must be unavailable before connection");
    });
    Console.WriteLine($"{passed} managed SDK tests passed");
    return 0;
}
catch (Exception error) { Console.Error.WriteLine(error); return 1; }


sealed class ExternalTestSource(bool blockStartup = false, uint framesPerSecond = 30) : IRealtimeExternalVideoSource
{
    public VideoFormat VideoFormat => new(16, 8, framesPerSecond);
    public ExternalFrameSink? Sink { get; private set; }
    public int StopCount { get; private set; }
    public TaskCompletionSource Entered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public TaskCompletionSource Stopped { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

    public Task StartAsync(
        ExternalFrameSink sink,
        CancellationToken cancellationToken
    )
    {
        Sink = sink;
        Entered.TrySetResult();
        return blockStartup ? Task.Delay(Timeout.Infinite, cancellationToken) : Task.CompletedTask;
    }

    public Task StopAsync()
    {
        ++StopCount;
        Sink = null;
        Stopped.TrySetResult();
        return Task.CompletedTask;
    }
}


sealed class TestCallbackContext : SynchronizationContext
{
    public override void Post(SendOrPostCallback callback, object? state)
    {
        ThreadPool.QueueUserWorkItem(_ =>
        {
            var previous = Current;
            SetSynchronizationContext(this);
            try { callback(state); }
            finally { SetSynchronizationContext(previous); }
        });
    }
}
