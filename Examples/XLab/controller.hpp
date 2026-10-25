#pragma once
#include "recording.hpp"
#include <QFuture>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVideoSink>
#include <atomic>
#include <map>
#include <memory>
#include <xmax/xmax.h>
struct SdkHandles {
    xmax_client client = nullptr;
    xmax_realtime realtime = nullptr;
    ~SdkHandles() {
        xmax_realtime_destroy(realtime);
        xmax_client_destroy(client);
    }
};
struct SdkUploadJob {
    xmax_storage storage = nullptr;
    double shown_progress = -1;
    std::atomic<bool> cancelled{false};
    void cancel() {
        cancelled = true;
        xmax_storage_cancel(storage);
    }
    ~SdkUploadJob() { xmax_storage_destroy(storage); }
};
class XLabController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY changed)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY changed)
    Q_PROPERTY(bool microphone MEMBER microphone NOTIFY changed)
    Q_PROPERTY(bool active MEMBER active NOTIFY changed)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY changed)
    Q_PROPERTY(bool closing MEMBER closing NOTIFY changed)
    Q_PROPERTY(bool preview MEMBER preview CONSTANT)
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY camerasChanged)
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId NOTIFY camerasChanged)
    Q_PROPERTY(QString cameraName READ cameraName NOTIFY camerasChanged)
    Q_PROPERTY(bool videoMirrored READ videoMirrored WRITE setVideoMirrored NOTIFY changed)
    Q_PROPERTY(bool statisticsVisible MEMBER statisticsVisible NOTIFY changed)
    Q_PROPERTY(QString error MEMBER error NOTIFY changed)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString prompt MEMBER prompt NOTIFY changed)
    Q_PROPERTY(QString selected MEMBER selected NOTIFY changed)
    Q_PROPERTY(QVariantList references READ references NOTIFY referencesChanged)
    Q_PROPERTY(bool remoteReady MEMBER remoteReady NOTIFY changed)
    Q_PROPERTY(QVariantMap metrics MEMBER metrics NOTIFY changed)
    Q_PROPERTY(QString elapsed READ elapsed NOTIFY changed)
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(QVideoSink* localSink READ localSink WRITE setLocalSink NOTIFY sinksChanged)
    Q_PROPERTY(QVideoSink* remoteSink READ remoteSink WRITE setRemoteSink NOTIFY sinksChanged)
    Q_PROPERTY(bool canRecord READ canRecord NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool finishingRecording READ finishingRecording NOTIFY changed)
    Q_PROPERTY(bool recordingToSave READ recordingToSave NOTIFY changed)
  public:
    explicit XLabController(bool uiPreview = false, bool sessionPreview = false, QObject* parent = nullptr);
    ~XLabController() override;
    QString apiKey() const { return key_; }
    void setApiKey(const QString&);
    QString model() const { return model_; }
    void setModel(const QString&);

    /// @brief Return SDK camera identities and names without opening a capture device.
    QVariantList cameras() const {
        return cameras_;
    }

    /// @brief Return the selected identity, or empty when an explicit choice is required.
    QString cameraId() const {
        return camera_id_;
    }

    /// @brief Select an enumerated camera while stopped and remember it for the next launch.
    void setCameraId(const QString&);

    /// @brief Return the selected device name for the selector and local video label.
    QString cameraName() const;

    bool videoMirrored() const { return video_mirrored_; }
    void setVideoMirrored(bool mirrored);

    /// @brief Refresh SDK devices while stopped without opening capture or a cloud session.
    Q_INVOKABLE void refreshCameras();

    /// Returns whether the system requests reduced interface animation.
    Q_INVOKABLE bool reduceMotion() const;

    QString mode() const { return mode_; }
    void setMode(const QString&);
    QVariantList references() const;
    Q_INVOKABLE QVariantList referencesFor(const QString&) const;
    QString elapsed() const;
    float volume() const { return volume_; }
    void setVolume(float);
    QVideoSink* localSink() const { return local_sink_; }
    void setLocalSink(QVideoSink* sink) {
        local_sink_ = sink;
        emit sinksChanged();
    }
    QVideoSink* remoteSink() const { return remote_sink_; }
    void setRemoteSink(QVideoSink* sink) {
        remote_sink_ = sink;
        emit sinksChanged();
    }
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void selectReference(const QString& id);
    Q_INVOKABLE void submitPrompt();
    Q_INVOKABLE void uploadReference(const QUrl& file, const QString& mode);
    Q_INVOKABLE void removeFreeReference();
    Q_INVOKABLE void dismissError() {
        error.clear();
        emit changed();
    }
    Q_INVOKABLE void interpolation();
    bool canRecord() const;
    bool recording() const { return recording_ != nullptr; }
    bool finishingRecording() const { return finishing_recording_; }
    bool recordingToSave() const { return !recording_path_.isEmpty(); }
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE QUrl defaultRecordingDestination() const;
    Q_INVOKABLE void saveRecording(const QUrl& destination);
    Q_INVOKABLE void discardRecording();
    Q_INVOKABLE void requestClose();
    bool microphone = false, active = false, busy = false, closing = false, preview = false,
         statisticsVisible = true, remoteReady = false;
    QString error, prompt, selected;
    QVariantMap metrics;
  signals:
    void apiKeyRequired();
    void changed();
    void sinksChanged();
    void referencesChanged();
    void camerasChanged();
    void recordingReady();
    void recordingSaveFailed(QString message);
    void readyToClose();

  private:
#ifdef XLAB_TEST_SUPPORT
    friend class RecordingTests;
#endif
    std::shared_ptr<RealtimeMediaRecorder> recording_, completed_recording_;
    QFuture<QString> recording_registration_;
    QString recording_path_;
    bool finishing_recording_ = false, saving_recording_ = false, close_requested_ = false;
    quint64 recording_id_ = 0;
    bool close_emitted_ = false;
    void checkCloseReady();
    QString key_, model_ = "x2.1", mode_ = "charx";
    float volume_ = 0;
    QVariantList presets_, uploads_;
    QVariantList cameras_;
    QString camera_id_;
    bool video_mirrored_ = true;
    std::shared_ptr<SdkHandles> handles_;
    QVideoSink* local_sink_ = nullptr;
    QVideoSink* remote_sink_ = nullptr;
    QTimer poll_, elapsed_timer_;
    QElapsedTimer session_clock_;
    quint64 lifecycle_ = 0, intent_ = 0;
    bool updating_ = false;
    QVariantMap pending_;
    std::map<QString, std::shared_ptr<SdkUploadJob>> transfers_;
    void tick();
    void beginGeneration();
    void applyPending();
    QFuture<QString> async(std::function<QString()> work, std::function<void(QString)> done);
    void patchUpload(const QString&, const QVariantMap&);
    void upload(const QString& id, const QUrl&, const QString& mode, quint64 intent);
};
