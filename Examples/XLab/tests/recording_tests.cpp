//
//  recording_tests.cpp
//  XLab
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "controller.hpp"
#include "recording.hpp"
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVideoFrame>
#include <cmath>
#include <condition_variable>
#include <mutex>

namespace {
RecordingPacket video(
    qint64 time,
    QSize size = {96, 64},
    uint64_t stream = 1
) {
    auto image = std::make_shared<QImage>(size, QImage::Format_ARGB32);
    image->fill(Qt::red);
    QPainter painter(image.get());
    painter.fillRect(0, size.height() / 2, size.width(), size.height() / 2, Qt::blue);
    painter.end();
    RecordingPacket packet;
    packet.storage = image;
    auto& view = packet.view;
    view.struct_size = sizeof(view);
    view.type = XMAX_MEDIA_VIDEO;
    view.stream_id = stream;
    view.presentation_time = {time, 1000000};
    view.video.struct_size = sizeof(view.video);
    view.video.pixel_format = XMAX_BGRA;
    view.video.width = size.width();
    view.video.height = size.height();
    view.video.planes[0] = image->constBits();
    view.video.strides[0] = image->bytesPerLine();
    view.video.plane_sizes[0] = image->sizeInBytes();
    return packet;
}

RecordingPacket audio(
    qint64 time,
    uint32_t rate = 48000,
    uint32_t channels = 1
) {
    auto pcm = std::make_shared<QByteArray>(int(rate / 100 * channels * 2), Qt::Uninitialized);
    auto samples = reinterpret_cast<int16_t*>(pcm->data());
    for (uint32_t sample = 0; sample < rate / 100; ++sample)
        for (uint32_t channel = 0; channel < channels; ++channel)
            samples[sample * channels + channel] =
                int16_t(std::sin(sample * 440.0 * 6.2831853 / rate) * 12000);
    RecordingPacket packet;
    packet.storage = pcm;
    auto& view = packet.view;
    view.struct_size = sizeof(view);
    view.type = XMAX_MEDIA_AUDIO;
    view.stream_id = 1;
    view.presentation_time = {time, 1000000};
    view.duration = {rate / 100, int32_t(rate)};
    view.audio = {sizeof(xmax_audio_frame),
                  rate,
                  channels,
                  rate / 100,
                  reinterpret_cast<const uint8_t*>(pcm->constData()),
                  size_t(pcm->size())};
    return packet;
}

struct EncodedSamples {
    QVector<QImage> images;
    QVector<qint64> videoTimes, audioTimes;
    QVector<QByteArray> pcm;
    int finishes = 0;
};

class ProbeWriter : public RecordingWriter {
  public:

    std::shared_ptr<EncodedSamples> samples;

    explicit ProbeWriter(
        std::shared_ptr<EncodedSamples> value
    )
        : samples(std::move(value)) {
    }

    void open(
        const QString& path,
        QSize
    ) override {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("finalized recording test") < 0)
            throw std::runtime_error("Cannot create test recording");
    }

    void video(
        const QImage& image,
        qint64 time,
        qint64
    ) override {
        samples->images.push_back(image);
        samples->videoTimes.push_back(time);
    }

    void audio(
        const QByteArray& pcm,
        qint64 time,
        qint64
    ) override {
        samples->pcm.push_back(pcm);
        samples->audioTimes.push_back(time);
    }

    void finish() override {
        ++samples->finishes;
    }
};

auto factory(
    const std::shared_ptr<EncodedSamples>& samples
) {
    return [samples] { return std::make_unique<ProbeWriter>(samples); };
}
} // namespace

class RecordingTests : public QObject {
    Q_OBJECT
  private slots:

    void commonOriginTrimmingGapsAndFixedCanvas() {
        auto samples = std::make_shared<EncodedSamples>();
        RealtimeMediaRecorder recorder({}, factory(samples));
        recorder.append(audio(995000)); // Half of this packet precedes the video origin.
        recorder.append(video(1000000));
        recorder.append(video(1033333, {48, 64}));
        recorder.append(video(1033333)); // Duplicate video PTS must not be encoded twice.
        recorder.append(audio(1200000)); // Keep the 195-ms gap instead of concatenating PCM.
        auto completion = recorder.finish();
        const auto result = completion.get();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(recorder.finish().get().path, result.path);
        QCOMPARE(samples->finishes, 1);
        QCOMPARE(samples->videoTimes, QVector<qint64>({0, 333330}));
        QCOMPARE(samples->audioTimes, QVector<qint64>({0, 2000000}));
        QCOMPARE(samples->pcm[0].size(), 480); // 240 remaining mono samples.
        QCOMPARE(samples->images[1].size(), QSize(96, 64));
        QCOMPARE(samples->images[1].pixelColor(0, 10), QColor(Qt::black));
        QVERIFY(samples->images[0].pixelColor(48, 4).red() > 200);
        recorder.append(video(2000000));
        QCOMPARE(samples->videoTimes.size(), 2);
    }

    void streamClockAndAudioFormatInterruptions_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("stream identity") << 0;
        QTest::newRow("video clock reset") << 1;
        QTest::newRow("audio format change") << 2;
    }

    void streamClockAndAudioFormatInterruptions() {
        QFETCH(int, failure);
        auto samples = std::make_shared<EncodedSamples>();
        QString reason;
        RealtimeMediaRecorder recorder([&](QString message) { reason = message; }, factory(samples));
        recorder.append(video(1000000));
        recorder.append(audio(1000000));
        if (failure == 0)
            recorder.append(video(1033333, {96, 64}, 2));
        if (failure == 1)
            recorder.append(video(900000));
        if (failure == 2)
            recorder.append(audio(1010000, 44100, 2));
        const auto result = recorder.finish().get();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!reason.isEmpty());
        QCOMPARE(samples->finishes, 1);
        QCOMPARE(samples->videoTimes.size(), 1);
    }

    void noVideoAndSaveFailurePreserveCompletedClip() {
        auto samples = std::make_shared<EncodedSamples>();
        RealtimeMediaRecorder empty({}, factory(samples));
        empty.append(audio(0));
        QVERIFY(empty.finish().get().error.contains("No remote video"));
        RealtimeMediaRecorder recorder({}, factory(samples));
        recorder.append(video(0));
        const auto result = recorder.finish().get();
        QVERIFY(result.error.isEmpty());
        QTemporaryDir directory;
        QVERIFY(
            !RealtimeMediaRecorder::save(result.path, directory.filePath("missing/failure.mp4")).isEmpty()
        );
        QVERIFY(QFile::exists(result.path));
        const auto destination = directory.filePath("保存录像.mp4");
        QFile existing(destination);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("old recording");
        existing.close();
        QVERIFY(RealtimeMediaRecorder::save(result.path, destination).isEmpty());
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("finalized recording test"));
        QVERIFY(QFile::exists(result.path));
    }

    void overflowKeepsQueueBoundedAndFinalizesPrefix() {
        struct Gate {
            std::mutex mutex;
            std::condition_variable changed;
            bool entered = false, released = false;
        };

        class SlowWriter : public ProbeWriter {
            std::shared_ptr<Gate> gate_;

          public:

            SlowWriter(
                std::shared_ptr<EncodedSamples> samples,
                std::shared_ptr<Gate> gate
            )
                : ProbeWriter(std::move(samples)), gate_(std::move(gate)) {
            }

            void video(
                const QImage& image,
                qint64 time,
                qint64 duration
            ) override {
                {
                    std::unique_lock<std::mutex> lock(gate_->mutex);
                    gate_->entered = true;
                    gate_->changed.notify_one();
                    gate_->changed.wait_for(lock, std::chrono::seconds(3), [&] { return gate_->released; });
                }
                ProbeWriter::video(image, time, duration);
            }
        };

        auto gate = std::make_shared<Gate>();
        auto samples = std::make_shared<EncodedSamples>();
        QString reason;
        RealtimeMediaRecorder recorder(
            [&](QString message) { reason = message; },
            [samples, gate] { return std::make_unique<SlowWriter>(samples, gate); }
        );
        recorder.append(video(0));
        {
            std::unique_lock<std::mutex> lock(gate->mutex);
            QVERIFY(gate->changed.wait_for(lock, std::chrono::seconds(3), [&] { return gate->entered; }));
        }
        for (int n = 1; n <= 200; ++n)
            recorder.append(video(n * 33333));
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            gate->released = true;
        }
        gate->changed.notify_one();
        QVERIFY(recorder.finish().get().error.isEmpty());
        QVERIFY(reason.contains("could not keep up"));
        QVERIFY(samples->images.size() <= 128);
        QCOMPARE(samples->finishes, 1);
    }

    void closeWaitsForFinalizeAndSaveRetryOrDiscard() {
        XLabController controller(true);
        auto samples = std::make_shared<EncodedSamples>();
        controller.active = true;
        controller.recording_ =
            std::make_shared<RealtimeMediaRecorder>(RealtimeMediaRecorder::Interruption{}, factory(samples));
        controller.recording_->append(video(0));
        QSignalSpy ready(&controller, &XLabController::recordingReady);
        QSignalSpy closed(&controller, &XLabController::readyToClose);
        QSignalSpy failed(&controller, &XLabController::recordingSaveFailed);
        controller.requestClose();
        controller.stopRecording(); // Repeated Stop joins the same finalization.
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(controller.recordingToSave());
        QCOMPARE(closed.count(), 0);
        QTemporaryDir directory;
        controller.saveRecording(QUrl::fromLocalFile(directory.filePath("missing/fail.mp4")));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(controller.recordingToSave());
        QCOMPARE(closed.count(), 0);
        controller.saveRecording(QUrl::fromLocalFile(directory.filePath("saved.mp4")));
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(!controller.recordingToSave());
        QVERIFY(QFile::exists(directory.filePath("saved.mp4")));
        controller.requestClose();
        QCOMPARE(closed.count(), 1);
        QCOMPARE(samples->finishes, 1);
    }

    void recordButtonCannotStartWithoutLiveRemoteMedia() {
        XLabController controller(true, true);
        QVERIFY(!controller.canRecord());
        controller.startRecording();
        QVERIFY(!controller.recording());
        controller.remoteReady = true;
        QVERIFY(!controller.canRecord());
    }

    void stoppingSessionAllowsDiscardAndRemovesTemporaryClip() {
        XLabController controller(true);
        auto samples = std::make_shared<EncodedSamples>();
        controller.active = true;
        controller.recording_ =
            std::make_shared<RealtimeMediaRecorder>(RealtimeMediaRecorder::Interruption{}, factory(samples));
        controller.recording_->append(video(0));
        QSignalSpy ready(&controller, &XLabController::recordingReady);
        controller.stop();
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(!controller.active && !controller.recording());
        const auto path = controller.recording_path_;
        QVERIFY(QFile::exists(path));
        controller.discardRecording();
        QVERIFY(!controller.recordingToSave());
        QTRY_VERIFY(!QFile::exists(path));
        QCOMPARE(samples->finishes, 1);
    }

    void encoderFailureAndByteLimitKeepRecordedPrefix() {
        class FailingWriter : public ProbeWriter {
          public:

            using ProbeWriter::ProbeWriter;

            void video(
                const QImage& image,
                qint64 time,
                qint64 duration
            ) override {
                if (time > 0)
                    throw std::runtime_error("Encoder stopped accepting video");
                ProbeWriter::video(image, time, duration);
            }
        };

        auto samples = std::make_shared<EncodedSamples>();
        QString reason;
        RealtimeMediaRecorder failure(
            [&](QString message) { reason = message; },
            [samples] { return std::make_unique<FailingWriter>(samples); }
        );
        failure.append(video(0));
        failure.append(video(33333));
        QVERIFY(failure.finish().get().error.isEmpty());
        QVERIFY(reason.contains("Encoder stopped"));
        QCOMPARE(samples->finishes, 1);

        auto boundedSamples = std::make_shared<EncodedSamples>();
        RealtimeMediaRecorder bounded([&](QString message) { reason = message; }, factory(boundedSamples));
        bounded.append(video(0));
        auto oversized = video(33333);
        oversized.view.video.plane_sizes[0] = 64 * 1024 * 1024 + 1;
        bounded.append(std::move(oversized));
        QVERIFY(bounded.finish().get().error.isEmpty());
        QVERIFY(reason.contains("could not keep up"));
        QCOMPARE(boundedSamples->images.size(), 1);
    }

    void windowsVideoWithoutAudioRemainsPlayable_data() {
        QTest::addColumn<QSize>("canvas");
        QTest::newRow("portrait") << QSize(1024, 1920);
        QTest::newRow("landscape") << QSize(1920, 1024);
    }

    void windowsVideoWithoutAudioRemainsPlayable() {
#ifndef Q_OS_WIN
        QSKIP("Native Windows encoder verification.");
#else
        QFETCH(QSize, canvas);
        RealtimeMediaRecorder recorder({});
        recorder.append(video(0, canvas));
        recorder.append(video(1000000, canvas));
        const auto result = recorder.finish().get();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QMediaPlayer player;
        QVideoSink sink;
        player.setVideoSink(&sink);
        player.setSource(QUrl::fromLocalFile(result.path));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(
            sink.videoFrame().isValid() || player.error() != QMediaPlayer::NoError, 10000
        );
        QVERIFY2(player.error() == QMediaPlayer::NoError, qPrintable(player.errorString()));
        QVERIFY(sink.videoFrame().isValid());
        QCOMPARE(sink.videoFrame().size(), canvas);
        QVERIFY(player.duration() >= 1000 && player.duration() <= 1200);
        player.stop();
        player.setSource({});
#endif
    }

    void windowsMp4ContainsDecodableVideoAudioAndSharedTiming() {
#ifndef Q_OS_WIN
        QSKIP("The native MP4 encoder is verified on Windows; portable contracts run on both hosts.");
#else
        RealtimeMediaRecorder recorder({});
        for (int n = 0; n < 48; ++n) {
            recorder.append(video(1000000 + n * 41666, n < 24 ? QSize(96, 64) : QSize(48, 64)));
            if (n >= 6 && n < 42) {
                const auto time = 1000000 + n * 41666;
                recorder.append(audio(time));
                recorder.append(audio(time + 10000));
                recorder.append(audio(time + 20000));
            }
            QTest::qWait(20);
        }
        const auto result = recorder.finish().get();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(QFile::exists(result.path));
        QMediaPlayer player;
        QVideoSink sink;
        QAudioBufferOutput audioOutput;
        QAudioOutput playback;
        playback.setVolume(0);
        player.setAudioOutput(&playback);
        player.setAudioBufferOutput(&audioOutput);
        player.setVideoSink(&sink);
        QImage firstImage;
        qint64 firstAudioTime = -1;
        qint64 audioEnd = -1;
        float peak = 0;
        connect(&sink, &QVideoSink::videoFrameChanged, this, [&](const QVideoFrame& frame) {
            if (firstImage.isNull() && frame.isValid())
                firstImage = frame.toImage();
        });
        connect(
            &audioOutput, &QAudioBufferOutput::audioBufferReceived, this, [&](const QAudioBuffer& buffer) {
                if (!buffer.isValid())
                    return;
                if (firstAudioTime < 0)
                    firstAudioTime = buffer.startTime();
                audioEnd = std::max(audioEnd, buffer.startTime() + buffer.duration());
                if (buffer.format().sampleFormat() == QAudioFormat::Float)
                    for (int n = 0; n < buffer.sampleCount(); ++n)
                        peak = std::max(peak, std::abs(buffer.constData<float>()[n]));
                if (buffer.format().sampleFormat() == QAudioFormat::Int16)
                    for (int n = 0; n < buffer.sampleCount(); ++n)
                        peak = std::max(peak, std::abs(buffer.constData<int16_t>()[n] / 32768.f));
            }
        );
        player.setSource(QUrl::fromLocalFile(result.path));
        player.play();
        QTRY_VERIFY_WITH_TIMEOUT(
            player.mediaStatus() == QMediaPlayer::EndOfMedia || player.error() != QMediaPlayer::NoError, 15000
        );
        QVERIFY2(player.error() == QMediaPlayer::NoError, qPrintable(player.errorString()));
        QCOMPARE(firstImage.size(), QSize(96, 64));
        QVERIFY(firstImage.pixelColor(48, 4).red() > 160);
        QVERIFY(firstImage.pixelColor(48, 60).blue() > 160);
        QVERIFY2(
            firstAudioTime >= 200000 && firstAudioTime <= 300000,
            qPrintable(QString("Unexpected audio start: %1 us").arg(firstAudioTime))
        );
        QVERIFY(peak > 0.1f); // Playback mute must not silence the encoded remote PCM.
        QVERIFY2(
            audioEnd >= 1680000 && audioEnd <= 1840000,
            qPrintable(QString("Audio gaps lost: last time %1 us").arg(audioEnd))
        );
        QVERIFY(player.duration() >= 1900 && player.duration() <= 2200);
        player.stop();
        player.setSource({});
        const auto artifactDirectory = qEnvironmentVariable("XLAB_RECORDING_ARTIFACTS");
        if (!artifactDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(artifactDirectory));
            QVERIFY(RealtimeMediaRecorder::save(result.path, artifactDirectory + "/remote-av.mp4").isEmpty());
            QVERIFY(firstImage.save(artifactDirectory + "/decoded-video.png"));
        }
#endif
    }
};

int main(
    int argc,
    char** argv
) {
    QGuiApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    RecordingTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "recording_tests.moc"
