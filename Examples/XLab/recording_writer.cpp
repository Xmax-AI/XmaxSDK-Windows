//
//  recording_writer.cpp
//  XLab
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#include "recording.hpp"
#include <stdexcept>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>

namespace {
using Microsoft::WRL::ComPtr;

void check(
    HRESULT status,
    const char* operation
) {
    if (FAILED(status))
        throw std::runtime_error(QString("%1 (0x%2).")
                                     .arg(QString::fromLatin1(operation))
                                     .arg(quint32(status), 8, 16, QLatin1Char('0'))
                                     .toStdString());
}

/** Owns COM and Media Foundation on the recording worker, never the SDK callback or GUI thread. */
class MfRecordingWriter final : public RecordingWriter {
    bool com_ = false, mf_ = false;
    ComPtr<IMFSinkWriter> writer_;
    DWORD video_ = 0, audio_ = 0;
    QSize canvas_;
    std::optional<qint64> audioOrigin_;
    qint64 audioSamples_ = 0;

    void write(
        DWORD stream,
        const BYTE* bytes,
        DWORD count,
        qint64 time,
        qint64 duration
    ) {
        ComPtr<IMFMediaBuffer> buffer;
        check(MFCreateMemoryBuffer(count, &buffer), "Unable to allocate recording storage");
        BYTE* destination = nullptr;
        check(buffer->Lock(&destination, nullptr, nullptr), "Unable to access recording storage");
        std::memcpy(destination, bytes, count);
        check(buffer->Unlock(), "Unable to release recording storage");
        check(buffer->SetCurrentLength(count), "Unable to set recording storage length");
        ComPtr<IMFSample> sample;
        check(MFCreateSample(&sample), "Unable to create recording sample");
        check(sample->AddBuffer(buffer.Get()), "Unable to attach recording storage");
        check(sample->SetSampleTime(time), "Unable to set recording time");
        check(sample->SetSampleDuration(duration), "Unable to set recording duration");
        check(writer_->WriteSample(stream, sample.Get()), "Unable to encode recording sample");
    }

  public:

    ~MfRecordingWriter() override {
        writer_.Reset();
        if (mf_)
            MFShutdown();
        if (com_)
            CoUninitialize();
    }

    void open(
        const QString& path,
        QSize canvas
    ) override {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Unable to initialize recording COM");
        com_ = true;
        check(MFStartup(MF_VERSION), "Unable to initialize Media Foundation recording");
        mf_ = true;
        canvas_ = canvas;
        ComPtr<IMFAttributes> attributes;
        check(MFCreateAttributes(&attributes, 2), "Unable to configure recording writer");
        check(
            attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE),
            "Unable to configure recording timing"
        );
        check(
            MFCreateSinkWriterFromURL(
                reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, attributes.Get(), &writer_
            ),
            "Unable to open the MP4 recording"
        );

        ComPtr<IMFMediaType> video;
        check(MFCreateMediaType(&video), "Unable to configure H.264 recording");
        check(video->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Unable to set video type");
        check(video->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "Unable to select H.264 encoding");
        check(
            video->SetUINT32(
                MF_MT_AVG_BITRATE, std::clamp(canvas.width() * canvas.height() * 6, 2000000, 20000000)
            ),
            "Unable to set video bitrate"
        );
        check(
            video->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Unable to set scan mode"
        );
        check(
            MFSetAttributeSize(video.Get(), MF_MT_FRAME_SIZE, canvas.width(), canvas.height()),
            "Unable to set video size"
        );
        check(MFSetAttributeRatio(video.Get(), MF_MT_FRAME_RATE, 30, 1), "Unable to set nominal frame rate");
        check(
            MFSetAttributeRatio(video.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
            "Unable to set pixel aspect ratio"
        );
        check(writer_->AddStream(video.Get(), &video_), "Unable to add recording video stream");
        ComPtr<IMFMediaType> videoInput;
        check(MFCreateMediaType(&videoInput), "Unable to create video input type");
        check(video->CopyAllItems(videoInput.Get()), "Unable to configure video input");
        check(videoInput->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Unable to set video input format");
        check(
            videoInput->SetUINT32(MF_MT_DEFAULT_STRIDE, canvas.width() * 4), "Unable to set video row layout"
        );
        check(
            writer_->SetInputMediaType(video_, videoInput.Get(), nullptr),
            "Unable to initialize H.264 encoder"
        );

        ComPtr<IMFMediaType> audio;
        check(MFCreateMediaType(&audio), "Unable to configure AAC recording");
        check(audio->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Unable to set audio type");
        check(audio->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC), "Unable to select AAC encoding");
        check(audio->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000), "Unable to set audio rate");
        check(audio->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1), "Unable to set audio channels");
        check(audio->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Unable to set audio precision");
        check(audio->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000), "Unable to set audio bitrate");
        check(audio->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1), "Unable to set AAC layout");
        check(writer_->AddStream(audio.Get(), &audio_), "Unable to add recording audio stream");

        ComPtr<IMFMediaType> audioInput;
        check(MFCreateMediaType(&audioInput), "Unable to create audio input type");
        check(audio->CopyAllItems(audioInput.Get()), "Unable to configure audio input");
        check(audioInput->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Unable to set PCM input format");
        check(audioInput->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 96000), "Unable to set PCM bitrate");
        check(audioInput->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 2), "Unable to set PCM layout");
        check(
            writer_->SetInputMediaType(audio_, audioInput.Get(), nullptr), "Unable to initialize AAC encoder"
        );
        check(writer_->BeginWriting(), "Unable to start recording");
    }

    void video(
        const QImage& image,
        qint64 time,
        qint64 duration
    ) override {
        const auto pixels = image.convertToFormat(QImage::Format_ARGB32);
        write(video_, pixels.constBits(), DWORD(canvas_.width() * canvas_.height() * 4), time, duration);
    }

    void audio(
        const QByteArray& pcm,
        qint64 time,
        qint64 duration
    ) override {
        (void)duration;
        if (!audioOrigin_)
            audioOrigin_ = time;
        const auto target = qint64(std::llround((time - *audioOrigin_) * 48000.L / 10000000));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        // AAC counts PCM samples after its first timestamp. Keep source gaps as silent samples.
        while (audioSamples_ < target) {
            if (std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error(
                    "Recording stopped because the audio encoder could not keep up. Save the recorded "
                    "portion."
                );
            const auto count = std::min<qint64>(target - audioSamples_, 4800);
            const QByteArray silence(count * 2, 0);
            write(
                audio_, reinterpret_cast<const BYTE*>(silence.constData()), DWORD(silence.size()),
                *audioOrigin_ + audioSamples_ * 10000000 / 48000, count * 10000000 / 48000
            );
            audioSamples_ += count;
        }
        const auto count = pcm.size() / 2;
        write(
            audio_, reinterpret_cast<const BYTE*>(pcm.constData()), DWORD(pcm.size()),
            *audioOrigin_ + audioSamples_ * 10000000 / 48000, count * 10000000 / 48000
        );
        audioSamples_ += count;
    }

    void finish() override {
        check(writer_->Finalize(), "Unable to finalize the MP4 recording");
        writer_.Reset();
    }
};
} // namespace
#endif

std::unique_ptr<RecordingWriter> makeRecordingWriter() {
#ifdef Q_OS_WIN
    return std::make_unique<MfRecordingWriter>();
#else
    throw std::runtime_error("MP4 recording is available in the Windows XLab build.");
#endif
}
