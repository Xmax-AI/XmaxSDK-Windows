#include "controller.hpp"
#include "rounded_image.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class BehaviorTests : public QObject {
    Q_OBJECT
  private slots:
    void init() {
        QSettings().remove("xmax.xlab.cameraId");
        QSettings().remove("xmax.xlab.videoMirrored");
    }

    void displayMirrorIsRememberedWithoutStartingCapture() {
        XLabController controller;
        QVERIFY(controller.videoMirrored());
        controller.setVideoMirrored(false);
        QVERIFY(!controller.videoMirrored());
        QVERIFY(!controller.active && !controller.busy);

        XLabController reopened;
        QVERIFY(!reopened.videoMirrored());
        reopened.setVideoMirrored(true);
        QVERIFY(reopened.videoMirrored());

        XLabController preview(true);
        preview.setVideoMirrored(false);
        QVERIFY(QSettings().value("xmax.xlab.videoMirrored").toBool());
    }

    void cameraSelectionIsExplicitAndRemembered() {
        XLabController controller;
        const auto cameras = controller.cameras();
        if (cameras.isEmpty())
            QSKIP("No cameras attached; device selection is validated on the Windows VM");
        if (cameras.size() > 1) {
            QVERIFY(controller.cameraId().isEmpty());
            QVERIFY(controller.cameraName().isEmpty());
        } else {
            QCOMPARE(controller.cameraId(), cameras.first().toMap()["id"].toString());
        }
        const auto chosen = cameras.last().toMap();
        controller.setCameraId(chosen["id"].toString());
        QCOMPARE(controller.cameraName(), chosen["name"].toString());
        controller.refreshCameras();
        QCOMPARE(controller.cameraId(), chosen["id"].toString());
        controller.setCameraId("not-an-enumerated-camera");
        QCOMPARE(controller.cameraId(), chosen["id"].toString());
        XLabController reopened;
        QCOMPARE(reopened.cameraId(), chosen["id"].toString());
        QCOMPARE(reopened.cameraName(), chosen["name"].toString());
        reopened.active = true;
        reopened.setCameraId({});
        QCOMPARE(reopened.cameraId(), chosen["id"].toString());
        reopened.active = false;
        reopened.busy = true;
        reopened.setCameraId({});
        QCOMPARE(reopened.cameraId(), chosen["id"].toString());
        reopened.busy = false;
    }

    void unavailableSavedCameraNeverFallsBack() {
        QSettings().setValue("xmax.xlab.cameraId", "disconnected-test-camera");
        XLabController controller;
        QCOMPARE(controller.cameraId(), QString("disconnected-test-camera"));
        QVERIFY(controller.cameraName().isEmpty());
        controller.refreshCameras();
        QCOMPARE(controller.cameraId(), QString("disconnected-test-camera"));
        QVERIFY(controller.cameraName().isEmpty());
        controller.setApiKey("test-key");
        controller.start();
        QVERIFY(!controller.active && !controller.busy);
        QVERIFY2(
            controller.error.contains("camera") || controller.error.contains("Live video is unavailable"),
            qPrintable(controller.error));
    }

    void previewDoesNotEnumerateOrChangeCamera() {
        QSettings().setValue("xmax.xlab.cameraId", "saved-camera");
        XLabController controller(true);
        QVERIFY(controller.cameras().isEmpty());
        QVERIFY(controller.cameraId().isEmpty());
        controller.refreshCameras();
        controller.setCameraId("preview-camera");
        QVERIFY(controller.cameras().isEmpty());
        QVERIFY(controller.cameraId().isEmpty());
        QCOMPARE(QSettings().value("xmax.xlab.cameraId").toString(), QString("saved-camera"));
    }

    void presetAddressesUseCurrentCdn() {
        XLabController controller(true);
        int count = 0;
        for (const auto& mode : {"charx", "clothx", "vibex"}) {
            for (const auto& item : controller.referencesFor(QString::fromLatin1(mode))) {
                const auto entry = item.toMap();
                for (const auto& field : {"thumbnail", "reference"}) {
                    const QUrl url(entry[QString::fromLatin1(field)].toString());
                    QCOMPARE(url.scheme(), QString("https"));
                    QCOMPARE(url.host(), QString("cdn.22duck.cn"));
                    QVERIFY(url.path().startsWith("/images/source/" + QString::fromLatin1(mode) + "/"));
                    QVERIFY(url.query().contains("presetVersion="));
                }
                ++count;
            }
        }
        QCOMPARE(count, 60);
    }

    void missingKeyBlocksLiveSession() {
        XLabController controller;
        controller.setApiKey({});
        QSignalSpy required(&controller, &XLabController::apiKeyRequired);
        controller.start();
        QVERIFY(!controller.active && !controller.busy);
        QVERIFY(controller.error.contains("API Key"));
        QCOMPARE(required.count(), 1);
        controller.setApiKey(" \t\n ");
        controller.start();
        QVERIFY(controller.apiKey().isEmpty());
        QVERIFY(!controller.active && !controller.busy);
        QCOMPARE(required.count(), 2);
    }

    void explicitPreviewNeverPersistsKey() {
        QSettings().setValue("xmax.xlab.apiKey", "test-saved-key");
        XLabController controller(true);
        QVERIFY(controller.preview);
        QVERIFY(controller.apiKey().isEmpty());
        controller.setApiKey("preview-only-key");
        controller.start();
        QVERIFY(controller.active && !controller.busy);
        QCOMPARE(QSettings().value("xmax.xlab.apiKey").toString(), QString("test-saved-key"));
        controller.stop();
        QVERIFY(!controller.active && !controller.busy);
    }

    void unavailableRtcStaysOnLanding() {
        const xmax_client_config config{sizeof(config), XMAX_ABI_VERSION, "test-key", "x2.1",
                                        1000};
        xmax_client client = nullptr;
        QCOMPARE(xmax_client_create(&config, &client), XMAX_OK);
        int32_t rtc = 0;
        const auto result = xmax_client_has_rtc(client, &rtc);
        xmax_client_destroy(client);
        QCOMPARE(result, XMAX_OK);
        if (rtc)
            QSKIP("RTC is present; live startup requires real device/service acceptance");
        XLabController controller;
        controller.setApiKey("test-key");
        controller.start();
        QVERIFY(!controller.active && !controller.busy);
        QVERIFY(controller.error.contains("Live video is unavailable"));
    }

    void thumbnailFormats_data() {
        QTest::addColumn<QByteArray>("format");
        QTest::newRow("png") << QByteArray("PNG");
        QTest::newRow("webp") << QByteArray("WEBP");
    }

    void thumbnailFormats() {
        QFETCH(QByteArray, format);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("thumbnail." + QString::fromLatin1(format).toLower());
        QImage original(240, 120, QImage::Format_ARGB32);
        original.fill(QColor("#e13b42"));
        QVERIFY2(original.save(path, format.constData()), "PNG/WebP image plugin must be available");
        RoundedImage thumbnail;
        thumbnail.setSize(QSizeF(64, 64));
        thumbnail.setSource(QUrl::fromLocalFile(path));
        QCOMPARE(thumbnail.status(), RoundedImage::Ready);
        QVERIFY(thumbnail.errorString().isEmpty());
        QImage painted(64, 64, QImage::Format_ARGB32_Premultiplied);
        painted.fill(Qt::transparent);
        QPainter painter(&painted);
        thumbnail.paint(&painter);
        painter.end();
        QVERIFY(painted.pixelColor(32, 32).red() > 180);
        QCOMPARE(painted.pixelColor(0, 0).alpha(), 0);
        // Decoded cache must survive recreation of a QML delegate.
        QVERIFY(QFile::remove(path));
        RoundedImage cached;
        cached.setSource(thumbnail.source());
        QCOMPARE(cached.status(), RoundedImage::Ready);
        thumbnail.setSource({});
        QCOMPARE(thumbnail.status(), RoundedImage::Null);
    }

    void missingThumbnailCanRetry() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("retry.png");
        RoundedImage thumbnail;
        thumbnail.setSource(QUrl::fromLocalFile(path));
        QCOMPARE(thumbnail.status(), RoundedImage::Error);
        QVERIFY(!thumbnail.errorString().isEmpty());
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path));
        thumbnail.retry();
        QCOMPARE(thumbnail.status(), RoundedImage::Ready);
        QVERIFY(thumbnail.errorString().isEmpty());
    }

    void corruptThumbnailIsVisibleFailure() {
        QTemporaryDir directory;
        QFile file(directory.filePath("invalid.png"));
        QVERIFY(file.open(QFile::WriteOnly));
        file.write("<html>image service unavailable</html>");
        file.close();
        RoundedImage thumbnail;
        thumbnail.setSource(QUrl::fromLocalFile(file.fileName()));
        QCOMPARE(thumbnail.status(), RoundedImage::Error);
        QVERIFY(thumbnail.errorString().contains("decode"));
        thumbnail.setSource(QUrl("http://example.invalid/image.png"));
        QCOMPARE(thumbnail.status(), RoundedImage::Error);
        QVERIFY(thumbnail.errorString().contains("HTTPS"));
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("XmaxSDKTests");
    QCoreApplication::setApplicationName("XLabBehavior");
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    BehaviorTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "behavior_tests.moc"
