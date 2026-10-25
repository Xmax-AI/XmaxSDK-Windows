//
//  recording.cpp
//  XLab
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "recording.hpp"
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {
constexpr qint64 ticksPerSecond = 10000000;
constexpr size_t queueBytes = 64 * 1024 * 1024;
constexpr size_t queuePackets = 128;

std::optional<qint64> ticks(
    xmax_media_time time
) {
    if (time.timescale <= 0 || time.value < 0)
        return {};
    const long double value = static_cast<long double>(time.value) * ticksPerSecond / time.timescale;
    if (value > static_cast<long double>(std::numeric_limits<qint64>::max() / 2))
        return {};
    return qint64(value);
}

size_t packetBytes(
    const RecordingPacket& packet
) {
    if (packet.view.type == XMAX_MEDIA_AUDIO)
        return packet.view.audio.data_size;
    size_t bytes = 0;
    for (const auto size : packet.view.video.plane_sizes) {
        if (size > queueBytes || bytes > queueBytes - size)
            return queueBytes + 1;
        bytes += size;
    }
    return bytes;
}

QImage imageFromFrame(
    const xmax_video_frame& frame
) {
    if (!frame.width || !frame.height || frame.width > 8192 || frame.height > 8192)
        return {};
    if (frame.pixel_format == XMAX_BGRA) {
        if (!frame.planes[0] || frame.strides[0] < frame.width * 4 ||
            frame.plane_sizes[0] < frame.strides[0] * (frame.height - 1) + frame.width * 4)
            return {};
        return QImage(
                   frame.planes[0], int(frame.width), int(frame.height), qsizetype(frame.strides[0]),
                   QImage::Format_ARGB32
        )
            .copy();
    }
    if ((frame.pixel_format != XMAX_NV12 && frame.pixel_format != XMAX_I420) || frame.width < 2 ||
        frame.height < 2 || (frame.width & 1) || (frame.height & 1))
        return {};
    QVideoFrame video(QVideoFrameFormat(
        QSize(int(frame.width), int(frame.height)),
        frame.pixel_format == XMAX_NV12 ? QVideoFrameFormat::Format_NV12 : QVideoFrameFormat::Format_YUV420P
    ));
    if (!video.map(QVideoFrame::WriteOnly))
        return {};
    bool valid = true;
    for (int plane = 0; plane < video.planeCount(); ++plane) {
        const auto rows = plane ? frame.height / 2 : frame.height;
        const auto columns = plane && frame.pixel_format == XMAX_I420 ? frame.width / 2 : frame.width;
        if (!frame.planes[plane] || frame.strides[plane] < columns ||
            frame.plane_sizes[plane] < frame.strides[plane] * (rows - 1) + columns) {
            valid = false;
            break;
        }
        for (uint32_t row = 0; row < rows; ++row)
            std::memcpy(
                video.bits(plane) + row * video.bytesPerLine(plane),
                frame.planes[plane] + row * frame.strides[plane], columns
            );
    }
    video.unmap();
    return valid ? video.toImage() : QImage{};
}
} // namespace

RecordingPacket RecordingPacket::retain(
    xmax_media_frame frame
) {
    xmax_media_frame retained = nullptr;
    if (xmax_media_frame_retain(frame, &retained) != XMAX_OK)
        throw std::runtime_error("Unable to retain a recording packet.");
    RecordingPacket packet;
    packet.storage = std::shared_ptr<void>(retained, [](void* value) {
        xmax_media_frame_release(static_cast<xmax_media_frame>(value));
    });
    packet.view.struct_size = sizeof(packet.view);
    if (xmax_media_frame_get_view(retained, &packet.view) != XMAX_OK)
        throw std::runtime_error("Unable to read a recording packet.");
    return packet;
}

struct RealtimeMediaRecorder::State {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<RecordingPacket> pending;
    size_t bytes = 0, packets = 0;
    bool accepting = true, finishing = false, overflow = false;
    QString requestedFailure;
    Interruption interrupted;
    WriterFactory factory;
    std::promise<RecordingResult> promise;
    std::shared_future<RecordingResult> result = promise.get_future().share();
    QTemporaryDir temporary;
    std::thread worker;

    // Encoder and timeline state below belongs exclusively to worker.
    std::unique_ptr<RecordingWriter> writer;
    QSize canvas;
    uint64_t stream = 0;
    std::optional<qint64> origin, lastVideo, audioEnd;
    std::optional<std::pair<uint32_t, uint32_t>> audioFormat;
    std::deque<RecordingPacket> prefixAudio;
    size_t prefixBytes = 0;
    bool halted = false;
    QString interruption;

    State(
        Interruption callback,
        WriterFactory create
    )
        : interrupted(std::move(callback)), factory(std::move(create)) {
        worker = std::thread([this] { run(); });
    }

    void halt(
        QString message
    ) {
        if (halted)
            return;
        halted = true;
        interruption = std::move(message);
        {
            std::lock_guard<std::mutex> lock(mutex);
            accepting = false;
            finishing = true;
        }
        if (interrupted)
            interrupted(interruption);
    }

    void consume(
        const RecordingPacket& packet
    ) {
        const auto time = ticks(packet.view.presentation_time);
        if (!time || !packet.view.stream_id)
            return;
        if (stream && stream != packet.view.stream_id) {
            halt("The remote stream changed. Save this recording and start a new one.");
            return;
        }
        stream = packet.view.stream_id;
        if (packet.view.type == XMAX_MEDIA_VIDEO) {
            auto image = imageFromFrame(packet.view.video);
            if (image.isNull())
                throw std::runtime_error("Unable to read a recording video frame.");
            if (!origin) {
                if (!temporary.isValid())
                    throw std::runtime_error("Unable to create a temporary recording file.");
                canvas = QSize(std::max(2, image.width() / 2 * 2), std::max(2, image.height() / 2 * 2));
                writer = factory();
                writer->open(temporary.filePath("recording.mp4"), canvas);
                origin = time;
            }
            const auto relative = *time - *origin;
            if (lastVideo && relative < *lastVideo) {
                halt("The remote video clock restarted. Save this recording and start a new one.");
                return;
            }
            if (relative < 0 || (lastVideo && relative == *lastVideo))
                return;
            if (image.size() != canvas) {
                QImage fitted(canvas, QImage::Format_ARGB32);
                fitted.fill(Qt::black);
                QPainter painter(&fitted);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                const auto size = image.size().scaled(canvas, Qt::KeepAspectRatio);
                painter.drawImage(
                    QRect(
                        QPoint((canvas.width() - size.width()) / 2, (canvas.height() - size.height()) / 2),
                        size
                    ),
                    image
                );
                painter.end();
                image = std::move(fitted);
            }
            const auto duration = ticks(packet.view.duration).value_or(0);
            writer->video(image, relative, duration > 0 ? duration : ticksPerSecond / 30);
            lastVideo = relative;
            auto prefix = std::move(prefixAudio);
            prefixAudio.clear();
            prefixBytes = 0;
            for (const auto& audio : prefix) {
                appendAudio(audio);
                if (halted)
                    break;
            }
        } else if (packet.view.type == XMAX_MEDIA_AUDIO) {
            if (origin)
                appendAudio(packet);
            else {
                prefixAudio.push_back(packet);
                prefixBytes += packetBytes(packet);
                while (prefixAudio.size() > 100 || prefixBytes > 2 * 1024 * 1024) {
                    prefixBytes -= packetBytes(prefixAudio.front());
                    prefixAudio.pop_front();
                }
            }
        }
    }

    void appendAudio(
        const RecordingPacket& packet
    ) {
        const auto& frame = packet.view.audio;
        const auto time = ticks(packet.view.presentation_time);
        if (!time || !frame.data || frame.sample_rate < 8000 || frame.sample_rate > 192000 ||
            frame.channels < 1 || frame.channels > 2 || !frame.samples_per_channel ||
            frame.samples_per_channel > frame.sample_rate ||
            frame.samples_per_channel > frame.data_size / (frame.channels * 2))
            return;
        const auto format = std::make_pair(frame.sample_rate, frame.channels);
        if (audioFormat && audioFormat != format) {
            halt("The remote audio format changed. Save this recording and start a new one.");
            return;
        }
        const auto relative = *time - *origin;
        const auto lower = audioEnd.value_or(0);
        const auto skip = qint64(
            std::ceil(
                std::max<qint64>(0, lower - relative) * static_cast<long double>(frame.sample_rate) /
                ticksPerSecond
            )
        );
        if (skip >= frame.samples_per_channel) {
            if (audioEnd &&
                relative + qint64(frame.samples_per_channel) * ticksPerSecond / frame.sample_rate <
                    *audioEnd - ticksPerSecond)
                halt("The remote audio clock restarted. Save this recording and start a new one.");
            return;
        }
        audioFormat = format;
        const auto count = frame.samples_per_channel - skip;
        const auto begin = relative + skip * ticksPerSecond / frame.sample_rate;
        const auto samples = std::max<qint64>(1, count * 48000 / frame.sample_rate);
        QByteArray pcm(samples * 2, Qt::Uninitialized);
        // Native SDK output is already 48-kHz mono. Other valid PCM layouts are normalized for AAC.
        auto sampleAt = [&](qint64 index) {
            index = std::min<qint64>(index + skip, frame.samples_per_channel - 1);
            int value = 0;
            for (uint32_t channel = 0; channel < frame.channels; ++channel) {
                const auto* p = frame.data + (index * frame.channels + channel) * 2;
                value += int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
            }
            return double(value) / frame.channels;
        };
        for (qint64 index = 0; index < samples; ++index) {
            const double position = double(index) * frame.sample_rate / 48000;
            const auto left = qint64(position);
            const auto value = int16_t(
                std::lround(sampleAt(left) + (sampleAt(left + 1) - sampleAt(left)) * (position - left))
            );
            pcm[index * 2] = char(value & 255);
            pcm[index * 2 + 1] = char((uint16_t(value) >> 8) & 255);
        }
        const auto duration = samples * ticksPerSecond / 48000;
        writer->audio(pcm, begin, duration);
        audioEnd = begin + duration;
    }

    void run() noexcept {
        RecordingResult completed;
        try {
            for (;;) {
                RecordingPacket packet;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    changed.wait(lock, [this] { return finishing || !pending.empty(); });
                    if (pending.empty())
                        break;
                    packet = std::move(pending.front());
                    pending.pop_front();
                }
                if (!halted) {
                    try {
                        consume(packet);
                    } catch (const std::exception& e) {
                        halt(QString::fromUtf8(e.what()));
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    bytes -= packetBytes(packet);
                    --packets;
                }
            }
            if (overflow)
                halt("Recording stopped because the encoder could not keep up. Save the recorded portion.");
            if (!requestedFailure.isEmpty())
                halt(requestedFailure);
            if (!lastVideo)
                completed.error =
                    interruption.isEmpty() ? "No remote video was received during recording." : interruption;
            else {
                writer->finish();
                completed.path = temporary.filePath("recording.mp4");
            }
        } catch (const std::exception& e) {
            completed.error = QString::fromUtf8(e.what());
        } catch (...) {
            completed.error = "Unable to finish the recording.";
        }
        writer.reset();
        prefixAudio.clear();
        promise.set_value(std::move(completed));
    }
};

RealtimeMediaRecorder::RealtimeMediaRecorder(
    Interruption interrupted,
    WriterFactory factory
)
    : state_(std::make_unique<State>(std::move(interrupted), std::move(factory))) {
}

RealtimeMediaRecorder::~RealtimeMediaRecorder() {
    finish();
    state_->worker.join();
}

void RealtimeMediaRecorder::append(
    RecordingPacket packet
) {
    const auto bytes = packetBytes(packet);
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->accepting)
            return;
        if (state_->packets >= queuePackets || bytes > queueBytes - state_->bytes) {
            state_->accepting = false;
            state_->overflow = state_->finishing = true;
        } else {
            state_->bytes += bytes;
            ++state_->packets;
            state_->pending.push_back(std::move(packet));
        }
    }
    state_->changed.notify_one();
}

void RealtimeMediaRecorder::stopAccepting() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->accepting = false;
}

void RealtimeMediaRecorder::fail(
    QString message
) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->accepting)
            return;
        state_->accepting = false;
        state_->finishing = true;
        state_->requestedFailure = std::move(message);
    }
    state_->changed.notify_one();
}

std::shared_future<RecordingResult> RealtimeMediaRecorder::finish() {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->accepting = false;
        state_->finishing = true;
    }
    state_->changed.notify_one();
    return state_->result;
}

QString RealtimeMediaRecorder::save(
    const QString& source,
    const QString& destination
) {
    const QFileInfo inputInfo(source), outputInfo(destination);
    if (inputInfo.absolutePath() == outputInfo.absolutePath() ||
        (!outputInfo.canonicalFilePath().isEmpty() &&
         inputInfo.canonicalFilePath() == outputInfo.canonicalFilePath()))
        return "Choose a destination outside the temporary recording folder.";
    QFile input(source);
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !input.size() || !output.open(QIODevice::WriteOnly))
        return "Unable to open the recording or its save destination.";
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            return "Unable to save the recording. Check available disk space and try again.";
    }
    if (input.error() != QFileDevice::NoError)
        return "Unable to read the completed recording. Try saving again.";
    return output.commit() ? QString{} : "Unable to replace the destination file. Try another location.";
}
