//
//  recording.hpp
//  XLab
//  Copyright (c) 2026 XMAX.AI PTE. LTD.
//

#pragma once
#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <functional>
#include <future>
#include <memory>
#include <xmax/xmax.h>

/** Owns the immutable storage referenced by one remote media view. */
struct RecordingPacket {
    xmax_media_frame_view view{};
    std::shared_ptr<void> storage;

    /** Retains a callback-borrowed SDK packet without copying its pixels or PCM. */
    static RecordingPacket retain(xmax_media_frame frame);
};

/** Encodes already normalized samples; all calls belong to one private recording thread. */
class RecordingWriter {
  public:

    virtual ~RecordingWriter() = default;
    virtual void open(const QString& path, QSize canvas) = 0;
    virtual void video(const QImage& image, qint64 time, qint64 duration) = 0;
    virtual void audio(const QByteArray& pcm, qint64 time, qint64 duration) = 0;
    virtual void finish() = 0;
};

/** Creates the Windows H.264/AAC MP4 writer, using 100-nanosecond sample times. */
std::unique_ptr<RecordingWriter> makeRecordingWriter();

/** A finalized clip, or a failure that left no usable clip. */
struct RecordingResult {
    QString path;
    QString error;
};

/**
 * Records remote media on a bounded private queue. Playback volume and display mirroring do not affect
 * the encoded content. The first video defines both tracks' origin and a fixed aspect-fit output canvas.
 */
class RealtimeMediaRecorder {
  public:

    using WriterFactory = std::function<std::unique_ptr<RecordingWriter>()>;
    using Interruption = std::function<void(QString)>;

    explicit RealtimeMediaRecorder(Interruption interrupted, WriterFactory factory = makeRecordingWriter);
    ~RealtimeMediaRecorder();

    /** Enqueues retained media without waiting for encoding; overflow finalizes the recorded prefix. */
    void append(RecordingPacket packet);

    /** Rejects new SDK callbacks immediately, including callbacks already in flight during removal. */
    void stopAccepting();

    /** Stops after a callback admission failure and reports why the recorded prefix was finalized. */
    void fail(QString message);

    /** Drains accepted packets and returns the same completion for every stop request. */
    std::shared_future<RecordingResult> finish();

    /** Copies through a sibling staging file and atomically replaces the destination on success. */
    static QString save(const QString& source, const QString& destination);

  private:

    struct State;
    std::unique_ptr<State> state_;
};
