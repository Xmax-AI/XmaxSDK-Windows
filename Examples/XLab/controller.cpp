#include "controller.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QVideoFrame>
#include <QtConcurrent>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

bool XLabController::reduceMotion() const {
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled;
#elif defined(Q_OS_MACOS)
    QSettings accessibility(QDir::homePath() + "/Library/Preferences/com.apple.universalaccess.plist",
                            QSettings::NativeFormat);
    return accessibility.value("reduceMotion", false).toBool();
#else
    return false;
#endif
}

namespace {
QString sdkError(xmax_result result) {
    return result < 0 ? QString::fromUtf8(xmax_last_error()) : QString();
}
QString modePrompt(const QString& mode) {
    return mode == "charx"    ? QStringLiteral("视频中角色替换成参考图中角色")
           : mode == "clothx" ? QStringLiteral("视频中人物衣服替换成参考图中衣服")
                              : QStringLiteral("视频风格变为参考图指定的风格");
}
QString metric(int n, const QString& unit) {
    return n < 0 ? QStringLiteral("—") : QString::number(n) + " " + unit;
}
QString ms(double n) {
    return n < 0 ? QStringLiteral("—") : QString::number(n, 'f', 1) + " ms";
}
} // namespace
XLabController::XLabController(bool uiPreview, bool sessionPreview, QObject* parent)
    : QObject(parent), preview(uiPreview) {
    key_ = preview ? QString() : QSettings().value("xmax.xlab.apiKey").toString();
    camera_id_ = preview ? QString() : QSettings().value("xmax.xlab.cameraId").toString();
    video_mirrored_ = preview || QSettings().value("xmax.xlab.videoMirrored", true).toBool();
    if (!preview)
        refreshCameras();
    QFile f(":/qt/qml/XLab/assets/presets.json");
    if (f.open(QFile::ReadOnly))
        presets_ = QJsonDocument::fromJson(f.readAll()).array().toVariantList();
    active = preview && sessionPreview;
    if (active) {
        session_clock_.start();
        if (!presets_.isEmpty())
            selected = presets_.first().toMap()["id"].toString();
    }
    poll_.setInterval(33);
    connect(&poll_, &QTimer::timeout, this, &XLabController::tick);
    poll_.start();
    elapsed_timer_.setInterval(1000);
    connect(&elapsed_timer_, &QTimer::timeout, this, &XLabController::changed);
    elapsed_timer_.start();
}
XLabController::~XLabController() {
    poll_.stop();
    if (recording_)
        recording_->stopAccepting();
    for (const auto& job : transfers_)
        job.second->cancel();
    if (handles_)
        xmax_realtime_close(handles_->realtime);
    QThreadPool::globalInstance()->waitForDone();
    handles_.reset();
}
void XLabController::setApiKey(const QString& key) {
    if (active || busy)
        return;
    key_ = key.trimmed();
    if (!preview)
        QSettings().setValue("xmax.xlab.apiKey", key_);
    emit changed();
}
void XLabController::setModel(const QString& model) {
    if (active || busy || model != "x2.1")
        return;
    model_ = model;
    emit changed();
}
QString XLabController::cameraName() const {
    for (const auto& entry : cameras_) {
        const auto camera = entry.toMap();
        if (camera["id"].toString() == camera_id_)
            return camera["name"].toString();
    }
    return {};
}

void XLabController::setVideoMirrored(bool mirrored) {
    if (busy || video_mirrored_ == mirrored)
        return;

    if (handles_) {
        const xmax_video_mirror_config config{sizeof(config), mirrored, mirrored, 0};
        error = sdkError(xmax_realtime_set_video_mirror(handles_->realtime, &config));
        if (!error.isEmpty()) {
            emit changed();
            return;
        }
    }
    video_mirrored_ = mirrored;
    if (!preview)
        QSettings().setValue("xmax.xlab.videoMirrored", mirrored);
    emit changed();
}

void XLabController::setCameraId(const QString& id) {
    if (active || busy || preview)
        return;
    bool found = id.isEmpty();
    for (const auto& entry : cameras_)
        found |= entry.toMap()["id"].toString() == id;
    if (!found || camera_id_ == id)
        return;
    camera_id_ = id;
    QSettings().setValue("xmax.xlab.cameraId", camera_id_);
    emit camerasChanged();
}

void XLabController::refreshCameras() {
    if (active || busy || preview)
        return;
    error.clear();
    SdkHandles handles;
    const xmax_client_config config{sizeof(config), XMAX_ABI_VERSION, "", nullptr, 15000};
    auto message = sdkError(xmax_client_create(&config, &handles.client));
    xmax_camera_list list = nullptr;
    if (message.isEmpty())
        message = sdkError(xmax_client_list_cameras(handles.client, &list));
    const std::unique_ptr<xmax_camera_list_impl, decltype(&xmax_camera_list_release)> owned(
        list, xmax_camera_list_release);
    QVariantList cameras;
    for (size_t i = 0; message.isEmpty() && i < xmax_camera_list_count(list); ++i) {
        xmax_camera_info info{};
        info.struct_size = sizeof(info);
        message = sdkError(xmax_camera_list_get(list, i, &info));
        if (message.isEmpty())
            cameras.push_back(QVariantMap{{"id", QString::fromUtf8(info.id)},
                                         {"name", QString::fromUtf8(info.name)}});
    }
    if (!message.isEmpty()) {
        cameras_.clear();
        error = message;
    } else {
        cameras_ = std::move(cameras);
        // A remembered but unavailable device must never silently become another camera.
        if (camera_id_.isEmpty() && cameras_.size() == 1)
            camera_id_ = cameras_.first().toMap()["id"].toString();
    }
    emit camerasChanged();
    emit changed();
}

void XLabController::setMode(const QString& mode) {
    if (mode_ == mode)
        return;
    mode_ = mode;
    ++intent_;
    pending_.clear();
    error.clear();
    emit changed();
    emit referencesChanged();
}
void XLabController::interpolation() {
    error = handles_ ? sdkError(xmax_realtime_set_interpolation(handles_->realtime, 1))
                     : QStringLiteral("Frame interpolation is unavailable in this preview.");
    emit changed();
}
QVariantList XLabController::references() const {
    return referencesFor(mode_);
}
QVariantList XLabController::referencesFor(const QString& mode) const {
    QVariantList out;
    for (const auto& v : uploads_)
        if (v.toMap()["mode"] == mode)
            out.append(v);
    for (const auto& v : presets_)
        if (v.toMap()["mode"] == mode)
            out.append(v);
    return out;
}
QString XLabController::elapsed() const {
    auto sec = active && session_clock_.isValid() ? session_clock_.elapsed() / 1000 : 0;
    return QString("%1:%2").arg(sec / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'));
}
QFuture<QString> XLabController::async(std::function<QString()> work, std::function<void(QString)> done) {
    auto watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [watcher, done = std::move(done)] {
        auto error = watcher->result();
        watcher->deleteLater();
        done(error);
    });
    auto future = QtConcurrent::run([work = std::move(work)] {
        try {
            return work();
        } catch (const std::exception&) {
            return QString("Desktop operation failed.");
        }
    });
    watcher->setFuture(future);
    return future;
}
void XLabController::start() {
    if (busy || active || close_requested_)
        return;
    error.clear();
    if (preview) {
        active = true;
        mode_ = "charx";
        selected = presets_.first().toMap()["id"].toString();
        session_clock_.start();
        emit changed();
        return;
    }
    if (key_.isEmpty()) {
        error = "API Key is required — click the key button in the top-right corner to enter it.";
        emit changed();
        emit apiKeyRequired();
        return;
    }
    auto handles = std::make_shared<SdkHandles>();
    const auto key = key_.toUtf8(), model = model_.toUtf8();
    // Match the macOS XLab's opt-in business and performance diagnostics.
    error = sdkError(xmax_logger_configure(XMAX_LOG_ALL, XMAX_LOG_CHINESE));
    if (!error.isEmpty()) {
        emit changed();
        return;
    }
    xmax_client_config config{sizeof(config), XMAX_ABI_VERSION, key.constData(), model.constData(), 15000};
    error = sdkError(xmax_client_create(&config, &handles->client));
    if (error.isEmpty())
        error = sdkError(xmax_realtime_create(handles->client, &handles->realtime));
    int32_t available = 0;
    if (error.isEmpty())
        error = sdkError(xmax_client_has_rtc(handles->client, &available));
    if (error.isEmpty() && !available) {
#ifdef Q_OS_MACOS
        error = "Live video is unavailable in this macOS development build. Use the Windows demo for a live "
                "session.";
#else
        error = "Live video is unavailable in this build. Use the Windows demo with RTC support.";
#endif
    }
    if (!error.isEmpty()) {
        emit changed();
        return;
    }
    refreshCameras();
    if (!error.isEmpty())
        return;
    if (cameraName().isEmpty()) {
        error = cameras_.isEmpty()
                    ? "No camera is available. Connect a camera and try again."
                    : "Select a camera before starting. Choose your physical camera for live video.";
        emit changed();
        return;
    }
    handles_ = handles;
    volume_updating_ = false;
    volume_ready_ = false;
    ++lifecycle_;
    ++intent_;
    busy = true;
    closing = false;
    updating_ = false;
    mode_ = "charx";
    metrics.clear();
    pending_.clear();
    const auto epoch = lifecycle_;
    const bool mic = microphone;
    const auto camera = camera_id_.toUtf8();
    emit changed();
    async(
        [handles, mic, camera, mirrored = video_mirrored_] {
            const xmax_video_mirror_config mirror{sizeof(mirror), mirrored, mirrored, 0};
            auto error = sdkError(xmax_realtime_set_video_mirror(handles->realtime, &mirror));
            if (!error.isEmpty())
                return error;
            const xmax_camera_config config{sizeof(config), camera.constData(), {1920, 1024, 30}, mic};
            return sdkError(xmax_realtime_prepare_camera(handles->realtime, &config));
        },
        [this, epoch](QString message) {
            if (epoch != lifecycle_)
                return;
            if (!message.isEmpty()) {
                error = message;
                stop();
                return;
            }
            volume_ready_ = true;
            beginGeneration();
        });
}
void XLabController::beginGeneration() {
    const auto epoch = lifecycle_;
    const auto handles = handles_;
    const auto reference = presets_.first().toMap();
    const bool mic = microphone;
    error = sdkError(xmax_realtime_set_remote_volume(handles->realtime, volume_));
    if (!error.isEmpty()) {
        stop();
        return;
    }
    selected = reference["id"].toString();
    async(
        [handles, reference, mic] {
            const auto p = modePrompt("charx").toUtf8(), r = reference["reference"].toString().toUtf8();
            xmax_generation_context c{sizeof(c), p.constData(), r.constData(), 0};
            return sdkError(xmax_realtime_start(handles->realtime, &c, mic));
        },
        [this, epoch](QString message) {
            if (epoch != lifecycle_)
                return;
            busy = false;
            if (!message.isEmpty()) {
                error = message;
                stop();
            } else {
                active = true;
                session_clock_.start();
                emit changed();
            }
        });
}
void XLabController::stop() {
    stopRecording();
    if (closing) {
        emit changed();
        return;
    }
    ++lifecycle_;
    volume_updating_ = false;
    volume_ready_ = false;
    ++intent_;
    active = false;
    remoteReady = false;
    pending_.clear();
    uploads_.clear();
    selected.clear();
    emit referencesChanged();
    updating_ = false;
    for (const auto& job : transfers_)
        job.second->cancel();
    transfers_.clear();
    if (local_sink_)
        local_sink_->setVideoFrame({});
    if (remote_sink_)
        remote_sink_->setVideoFrame({});
    const auto handles = std::exchange(handles_, {});
    busy = bool(handles);
    closing = busy;
    emit changed();
    if (handles) {
        const auto epoch = lifecycle_;
        async([handles] { return sdkError(xmax_realtime_close(handles->realtime)); },
              [this, epoch](QString message) {
                  if (epoch != lifecycle_)
                      return;
                  busy = false;
                  closing = false;
                  if (!message.isEmpty())
                      error = message;
                  emit changed();
                  checkCloseReady();
              });
    } else {
        busy = false;
        closing = false;
        emit changed();
        checkCloseReady();
    }
}

bool XLabController::canRecord() const {
    return active && remoteReady && !preview && !busy && !closing && !close_requested_ &&
           !recording_ && !finishing_recording_ && recording_path_.isEmpty();
}

void XLabController::startRecording() {
    if (!canRecord() || !handles_)
        return;
    error.clear();
    QPointer<XLabController> guard(this);
    const auto id = ++recording_id_;
    auto recorder = std::make_shared<RealtimeMediaRecorder>([guard, id](QString message) {
        if (guard)
            QMetaObject::invokeMethod(guard, [guard, id, message] {
                if (guard && guard->recording_ && guard->recording_id_ == id) {
                    guard->error = message;
                    guard->stopRecording();
                }
            }, Qt::QueuedConnection);
    });
    recording_ = recorder;
    const auto handles = handles_;
    recording_registration_ = async([handles, recorder] {
        auto context = new std::shared_ptr<RealtimeMediaRecorder>(recorder);
        const xmax_remote_media_listener listener{
            sizeof(listener),
            [](void* user, xmax_media_frame frame) {
                try {
                    (*static_cast<std::shared_ptr<RealtimeMediaRecorder>*>(user))->append(
                        RecordingPacket::retain(frame));
                } catch (...) {
                    // Never propagate an application allocation failure through the SDK callback.
                    (*static_cast<std::shared_ptr<RealtimeMediaRecorder>*>(user))->fail(
                        "Recording stopped because a media packet could not be retained. Save the recorded portion.");
                }
            },
            context,
            [](void* user) { delete static_cast<std::shared_ptr<RealtimeMediaRecorder>*>(user); }
        };
        const auto result = xmax_realtime_set_remote_media_frame_listener(handles->realtime, &listener);
        if (result != XMAX_OK)
            delete context;
        return sdkError(result);
    }, [this, recorder](QString message) {
        if (recording_ == recorder && !message.isEmpty()) {
            error = message;
            stopRecording();
        }
    });
    emit changed();
}

void XLabController::stopRecording() {
    if (!recording_)
        return;
    auto recorder = std::exchange(recording_, {});
    recorder->stopAccepting();
    finishing_recording_ = true;
    const auto handles = handles_;
    auto registration = recording_registration_;
    auto result = std::make_shared<RecordingResult>();
    async([handles, recorder, registration, result]() mutable {
        // Removal follows registration even when Stop is pressed before registration finishes.
        registration.waitForFinished();
        if (handles)
            xmax_realtime_set_remote_media_frame_listener(handles->realtime, nullptr);
        *result = recorder->finish().get();
        return result->error;
    }, [this, recorder, result](QString message) {
        finishing_recording_ = false;
        if (!message.isEmpty())
            error = message;
        if (!result->path.isEmpty()) {
            completed_recording_ = recorder;
            recording_path_ = result->path;
            emit recordingReady();
        }
        emit changed();
        checkCloseReady();
    });
    emit changed();
}

QUrl XLabController::defaultRecordingDestination() const {
    auto directory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(directory + "/XLab-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmmss") + ".mp4");
}

void XLabController::saveRecording(const QUrl& destination) {
    if (recording_path_.isEmpty() || saving_recording_)
        return;
    if (!destination.isLocalFile()) {
        emit recordingSaveFailed("Choose a local folder for the MP4 recording.");
        return;
    }
    saving_recording_ = true;
    const auto source = recording_path_;
    async([source, destination] { return RealtimeMediaRecorder::save(source, destination.toLocalFile()); },
          [this](QString message) {
              saving_recording_ = false;
              if (message.isEmpty())
                  discardRecording();
              else
                  emit recordingSaveFailed(message);
              emit changed();
          });
    emit changed();
}

void XLabController::discardRecording() {
    if (saving_recording_)
        return;
    recording_path_.clear();
    completed_recording_.reset();
    emit changed();
    checkCloseReady();
}

void XLabController::requestClose() {
    close_requested_ = true;
    stop();
    checkCloseReady();
}

void XLabController::checkCloseReady() {
    if (close_requested_ && !close_emitted_ && !closing && !recording_ && !finishing_recording_ &&
        !saving_recording_ && recording_path_.isEmpty()) {
        close_emitted_ = true;
        emit readyToClose();
    }
}
void XLabController::selectReference(const QString& id) {
    if (!active || busy)
        return;
    for (const auto& v : references()) {
        auto r = v.toMap();
        if (r["id"] != id)
            continue;
        if (r["state"] == "uploading")
            return;
        const auto intent = ++intent_;
        if (r["state"] == "error") {
            upload(id, r["file"].toUrl(), mode_, intent);
            return;
        }
        selected = id;
        pending_ = r;
        pending_["prompt"] = mode_ == "free" ? prompt : modePrompt(mode_);
        applyPending();
        emit changed();
        return;
    }
}
void XLabController::submitPrompt() {
    if (!active || busy)
        return;
    for (const auto& item : referencesFor("free"))
        if (item.toMap()["state"] != "ready")
            return;
    ++intent_;
    pending_ = {{"prompt", prompt}, {"mode", "free"}};
    for (const auto& v : uploads_)
        if (v.toMap()["mode"] == "free")
            pending_["reference"] = v.toMap()["reference"];
    applyPending();
}
void XLabController::applyPending() {
    if (pending_.isEmpty() || updating_)
        return;
    const auto entry = std::exchange(pending_, {});
    if (preview)
        return;
    const auto handles = handles_;
    if (!handles)
        return;
    updating_ = true;
    const auto epoch = lifecycle_, intent = intent_;
    async(
        [handles, entry] {
            const auto p = entry["prompt"].toString().toUtf8(), r = entry["reference"].toString().toUtf8();
            xmax_generation_context c{sizeof(c), p.constData(), r.isEmpty() ? nullptr : r.constData(),
                                      entry["mode"] == "clothx"};
            return sdkError(xmax_realtime_update(handles->realtime, &c));
        },
        [this, epoch, intent](QString message) {
            if (epoch != lifecycle_)
                return;
            updating_ = false;
            if (intent == intent_ && !message.isEmpty())
                error = message;
            applyPending();
            emit changed();
        });
}
void XLabController::setVolume(float value) {
    if (!std::isfinite(value) || value < 0 || value > 1)
        return;
    volume_ = value;
    emit changed();
    applyVolume();
}
void XLabController::applyVolume() {
    if (!handles_ || !volume_ready_ || closing || volume_updating_)
        return;
    volume_updating_ = true;
    const auto handles = handles_;
    const auto epoch = lifecycle_;
    const auto value = volume_;
    async([handles, value] { return sdkError(xmax_realtime_set_remote_volume(handles->realtime, value)); },
          [this, epoch, value](QString message) {
              if (epoch != lifecycle_)
                  return;
              volume_updating_ = false;
              if (value != volume_) {
                  applyVolume();
                  return;
              }
              if (!message.isEmpty()) {
                  error = message;
                  emit changed();
              }
          });
}
void XLabController::tick() {
    if (!handles_)
        return;
    xmax_event e{};
    e.struct_size = sizeof(e);
    bool updated = false;
    while (xmax_realtime_poll_event(handles_->realtime, &e) == XMAX_OK) {
        if (e.type == XMAX_EVENT_ERROR) {
            error = QString::fromUtf8(e.message);
            stop();
            return;
        }
        if (e.type == XMAX_EVENT_STATISTICS) {
            const QString prefix = e.direction ? "down" : "up";
            if (e.width >= 0 && e.height >= 0)
                metrics[prefix + "Resolution"] = QString("%1 × %2").arg(e.width).arg(e.height);
            if (e.fps >= 0)
                metrics[prefix + "Fps"] = metric(e.fps, "fps");
            if (e.bitrate_kbps >= 0)
                metrics[prefix + "Bitrate"] = metric(e.bitrate_kbps, "kbps");
            if (e.packet_loss_percent >= 0)
                metrics[prefix + "Loss"] = metric(e.packet_loss_percent, "%");
            if (e.e2e_ms >= 0)
                metrics["e2e"] = metric(e.e2e_ms, "ms");
            const QStringList names{"Unknown", "Excellent", "Good",        "Poor",
                                    "Bad",     "Very bad",  "Disconnected"};
            if (e.quality >= 0 && e.quality < names.size())
                metrics[prefix + "Quality"] = names[e.quality];
            updated = true;
        }
        if (e.type == XMAX_EVENT_TIMING) {
            metrics["session"] = ms(e.session_ms);
            metrics["join"] = ms(e.rtc_join_ms);
            metrics["publish"] = ms(e.publish_ms);
            metrics["firstFrame"] = ms(e.first_frame_ms);
            metrics["total"] = ms(e.total_ms);
            updated = true;
        }
    }
    for (auto& item : transfers_) {
        xmax_transfer_progress progress{};
        progress.struct_size = sizeof(progress);
        if (xmax_storage_get_progress(item.second->storage, &progress) == XMAX_OK && progress.total) {
            const double value = double(progress.transferred) / double(progress.total);
            if (std::abs(value - item.second->shown_progress) >= 0.01) {
                item.second->shown_progress = value;
                patchUpload(item.first, {{"progress", value}});
            }
        }
    }
    xmax_frame local = nullptr;
    if (xmax_realtime_acquire_local_frame(handles_->realtime, &local) == XMAX_OK) {
        xmax_video_frame view{};
        view.struct_size = sizeof(view);
        if (local_sink_ && xmax_frame_get_view(local, &view) == XMAX_OK && view.pixel_format == XMAX_BGRA) {
            QImage image(view.planes[0], int(view.width), int(view.height), qsizetype(view.strides[0]),
                         QImage::Format_ARGB32);
            int32_t mirrored = 0;
            if (xmax_frame_get_mirrored(local, &mirrored) == XMAX_OK) {
                QVideoFrame frame(image.copy());
                frame.setMirrored(mirrored != 0);
                local_sink_->setVideoFrame(frame);
            }
        }
        xmax_frame_release(local);
    }
    xmax_frame frame = nullptr;
    if (xmax_realtime_acquire_frame(handles_->realtime, &frame) == XMAX_OK) {
        xmax_video_frame view{};
        view.struct_size = sizeof(view);
        if (remote_sink_ && xmax_frame_get_view(frame, &view) == XMAX_OK && view.pixel_format == XMAX_BGRA) {
            QImage image(view.planes[0], int(view.width), int(view.height), qsizetype(view.strides[0]),
                         QImage::Format_ARGB32);
            int32_t mirrored = 0;
            if (xmax_frame_get_mirrored(frame, &mirrored) == XMAX_OK) {
                QVideoFrame video(image.copy());
                video.setMirrored(mirrored != 0);
                remote_sink_->setVideoFrame(video);
            }
            if (!remoteReady) {
                remoteReady = true;
                updated = true;
            }
        }
        xmax_frame_release(frame);
    }
    if (updated)
        emit changed();
}
void XLabController::patchUpload(const QString& id, const QVariantMap& patch) {
    for (auto& v : uploads_) {
        auto r = v.toMap();
        if (r["id"] == id) {
            for (auto it = patch.begin(); it != patch.end(); ++it)
                r[it.key()] = it.value();
            v = r;
            break;
        }
    }
    emit changed();
    emit referencesChanged();
}
void XLabController::uploadReference(const QUrl& file, const QString& mode) {
    if (!active || busy)
        return;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (mode == "free")
        removeFreeReference();
    const auto intent = ++intent_;
    uploads_.prepend(QVariantMap{{"id", id},
                                 {"mode", mode},
                                 {"name", QFileInfo(file.toLocalFile()).fileName()},
                                 {"thumbnail", file},
                                 {"file", file},
                                 {"state", "uploading"},
                                 {"progress", 0}});
    emit changed();
    emit referencesChanged();
    upload(id, file, mode, intent);
}
void XLabController::removeFreeReference() {
    ++intent_;
    for (qsizetype i = uploads_.size(); i-- > 0;)
        if (uploads_[i].toMap()["mode"] == "free") {
            const auto id = uploads_[i].toMap()["id"].toString();
            const auto job = transfers_.find(id);
            if (job != transfers_.end()) {
                job->second->cancel();
                transfers_.erase(job);
            }
            uploads_.removeAt(i);
        }
    selected.clear();
    emit changed();
    emit referencesChanged();
}
void XLabController::upload(const QString& id, const QUrl& file, const QString& mode, quint64 intent) {
    if (preview) {
        patchUpload(id, {{"state", "error"}, {"error", "Uploads require a live session."}});
        return;
    }
    const auto handles = handles_;
    if (!handles)
        return;
    const auto epoch = lifecycle_;
    patchUpload(id, {{"state", "uploading"}, {"progress", 0}});
    auto job = std::make_shared<SdkUploadJob>();
    const auto creation = sdkError(xmax_storage_create(handles->client, &job->storage));
    if (!creation.isEmpty()) {
        patchUpload(id, {{"state", "error"}, {"error", creation}});
        return;
    }
    transfers_[id] = job;
    auto uploaded_url = std::make_shared<QString>();
    async(
        [handles, job, file, uploaded_url] {
            if (job->cancelled)
                return QStringLiteral("Upload cancelled.");
            const auto path = file.toLocalFile().toUtf8();
            xmax_uploaded_file uploaded = nullptr;
            const auto result = xmax_storage_upload_file(job->storage, path.constData(), nullptr, &uploaded);
            if (result < 0)
                return sdkError(result);
            xmax_uploaded_file_info info{};
            info.struct_size = sizeof(info);
            const auto status = xmax_uploaded_file_get_info(uploaded, &info);
            const auto error = sdkError(status);
            if (status == XMAX_OK)
                *uploaded_url = QString::fromUtf8(info.url);
            xmax_uploaded_file_release(uploaded);
            return error;
        },
        [this, job, uploaded_url, id, mode, intent, epoch](QString message) {
            if (epoch != lifecycle_)
                return;
            const auto current = transfers_.find(id);
            if (current == transfers_.end() || current->second != job)
                return;
            transfers_.erase(current);
            if (!message.isEmpty()) {
                patchUpload(id, {{"state", "error"}, {"error", message}});
                error = message;
                emit changed();
                return;
            }
            patchUpload(id, {{"state", "ready"}, {"progress", 1}, {"reference", *uploaded_url}});
            if (intent == intent_ && mode == mode_)
                selectReference(id);
        });
}
