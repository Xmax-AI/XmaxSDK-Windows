#include "controller.hpp"
#include <QDebug>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImageReader>
#include <QNetworkProxyFactory>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVideoFrame>
#ifdef XLAB_TEST_SUPPORT
#include <QTest>
#endif
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("XMAX.AI");
    QCoreApplication::setApplicationName("XLab");
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    QQuickStyle::setStyle("Basic");
    QImageReader::setAllocationLimit(128);
    const auto args = app.arguments();
    const bool keyGate = args.contains("--smoke-test-key-gate");
    const bool smoke = args.contains("--smoke-test") || keyGate;
    const bool preview =
        !keyGate && (smoke || args.contains("--preview") || args.contains("--preview-session"));
    QTemporaryDir testSettings;
    if (smoke) {
        if (!testSettings.isValid())
            return 5;
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testSettings.path());
    }
    auto value = [&args](const QString& flag) {
        auto i = args.indexOf(flag);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString();
    };
    const auto fonts = QFontDatabase::applicationFontFamilies(
        QFontDatabase::addApplicationFont(":/qt/qml/XLab/assets/DMSans.ttf"));
    if (!fonts.isEmpty()) {
        QFont font(fonts.first());
        font.setPixelSize(13);
        app.setFont(font);
    }
    XLabController controller(preview, args.contains("--preview-session"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("xlab", &controller);
    engine.rootContext()->setContextProperty("softwareRendering",
                                             qEnvironmentVariable("QT_QUICK_BACKEND") == "software");
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("XLab", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window)
        return 1;
    if (!value("--screenshot").isEmpty())
        QTimer::singleShot(1500, &app, [&] {
            auto image = window->grabWindow();
            bool ok = image.save(value("--screenshot"));
            app.exit(ok ? 0 : 2);
        });
#ifdef XLAB_TEST_SUPPORT
    if (smoke)
        QTimer::singleShot(600, &app, [&] {
            bool ok = true;
            auto verify = [&](bool pass, const char* message) {
                if (!pass) {
                    qCritical() << message;
                    ok = false;
                }
            };
            std::function<QQuickItem*(QQuickItem*, const QString&)> visualFind;
            visualFind = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
                if (parent->objectName() == name)
                    return parent;
                for (auto child : parent->childItems())
                    if (auto item = visualFind(child, name))
                        return item;
                return nullptr;
            };
            auto click = [&](const char* name) {
                auto item = window->findChild<QQuickItem*>(name);
                if (!item)
                    item = visualFind(window->contentItem(), QString::fromUtf8(name));
                if (!item) {
                    verify(false, name);
                    return;
                }
                const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
                QTest::qWait(100);
            };
            const auto dir = value("--screenshot-dir");
            if (!dir.isEmpty())
                QDir().mkpath(dir);
            verify(QImageReader::supportedImageFormats().contains("webp"),
                   "WebP thumbnail decoder is missing from this deployment");
            if (keyGate) {
                auto picker = window->findChild<QQuickItem*>("cameraButton");
                verify(picker && picker->isVisible(), "Live startup must show the camera selector");
                if (picker) {
                    const auto position = picker->mapToScene(QPointF());
                    verify(position.x() > window->width() / 2 && position.y() < 100,
                           "Camera selector must stay in the top-right header like macOS XmaxCamera");
                }
                if (picker && !controller.cameras().isEmpty()) {
                    if (controller.cameras().size() > 1)
                        verify(controller.cameraId().isEmpty(),
                               "Multiple cameras must require an explicit choice");
                    click("cameraButton");
                    const auto option = "cameraOption_" + QByteArray::number(controller.cameras().size() - 1);
                    click(option.constData());
                    verify(controller.cameraId() == controller.cameras().last().toMap()["id"].toString(),
                           "Camera selector must pass the chosen SDK device identity");
                    click("cameraButton");
                    click("cameraRefresh");
                    verify(!controller.cameraName().isEmpty(), "Refresh must retain the selected camera");
                    if (!dir.isEmpty())
                        verify(window->grabWindow().save(dir + "/camera-picker.png"),
                               "Cannot capture the header camera picker");
                    QTest::keyClick(window, Qt::Key_Escape);
                }
                click("startButton");
                verify(!controller.active && !controller.busy, "Empty key must not start a live session");
                verify(controller.error.contains("API Key"), "Missing key must report an error");
                verify(window->findChild<QQuickItem*>("keyInput")->hasActiveFocus(),
                       "Missing key must open and focus the API Key editor");
                controller.setApiKey(" \t ");
                QTest::keyClick(window, Qt::Key_Escape);
                click("startButton");
                verify(!controller.active && !controller.busy && controller.apiKey().isEmpty(),
                       "Whitespace key must not start a live session");
                if (!dir.isEmpty())
                    verify(window->grabWindow().save(dir + "/key-required.png"),
                           "Cannot capture API Key gate");
                qInfo() << (ok ? "X-Lab API Key gate test passed" : "X-Lab API Key gate test failed");
                app.exit(ok ? 0 : 3);
                return;
            }
            click("modelButton");
            QTest::keyClick(window, Qt::Key_Escape);
            window->contentItem()->forceActiveFocus();
            click("keyButton");
            verify(window->findChild<QQuickItem*>("keyInput")->hasActiveFocus(),
                   "API Key editor should focus input");
            QTest::keyClick(window, Qt::Key_Escape);
            if (!dir.isEmpty())
                verify(window->grabWindow().save(dir + "/landing.png"), "Cannot capture landing page");
            auto stage = window->findChild<QQuickItem*>("landingStage");
            verify(stage != nullptr, "Landing stage is missing");
            if (stage) {
                const auto image = window->grabWindow();
                const auto position =
                    stage->mapToScene(QPointF(stage->width() * 0.8, stage->height() * 0.25));
                const auto color = image.pixelColor(int(position.x() * image.width() / window->width()),
                                                    int(position.y() * image.height() / window->height()));
                verify(color.red() < 100 && color.green() < 100, "Checkerboard stage did not render");
            }
            click("startButton");
            verify(controller.active, "Start Camera should open preview session");
            // Check actual video pixels so UI overlays and stream data cannot hide a double flip.
            QImage direction(200, 100, QImage::Format_ARGB32);
            direction.fill(Qt::red);
            {
                QPainter painter(&direction);
                painter.fillRect(100, 0, 100, 100, Qt::blue);
            }
            for (const bool mirrored : {true, false}) {
                controller.setVideoMirrored(mirrored);
                QVideoFrame frame(direction);
                frame.setMirrored(mirrored);
                controller.localSink()->setVideoFrame(frame);
                controller.remoteSink()->setVideoFrame(frame);
                QTest::qWait(150);
                const auto image = window->grabWindow();
                if (!dir.isEmpty())
                    verify(image.save(dir + (mirrored ? "/mirror-on.png" : "/mirror-off.png")),
                           "Cannot capture video mirror verification");
                for (const auto* name : {"localVideoOutput", "remoteVideoOutput"}) {
                    auto output = window->findChild<QQuickItem*>(name);
                    verify(output != nullptr, "Video output is missing");
                    if (!output)
                        continue;
                    // Qt's software scene graph does not render VideoOutput pixels.
                    if (qEnvironmentVariable("QT_QUICK_BACKEND") == "software")
                        continue;
                    // Read screen coordinates from the untransformed panel, not the mirrored video.
                    auto panel = output->parentItem();
                    auto sample = [&](double x) {
                        const auto point = panel->mapToScene(QPointF(panel->width() * x, panel->height() * 0.82));
                        return image.pixelColor(int(point.x() * image.width() / window->width()),
                                                int(point.y() * image.height() / window->height()));
                    };
                    const auto left = sample(0.2), right = sample(0.8);
                    verify(mirrored ? left.blue() > 200 && right.red() > 200
                                    : left.red() > 200 && right.blue() > 200,
                           "Preview and result must follow the same display-only mirror setting");
                }
            }
            controller.setVideoMirrored(true);
            controller.localSink()->setVideoFrame({});
            controller.remoteSink()->setVideoFrame({});
            click("mode_clothx");
            verify(controller.mode() == "clothx", "Virtual Try-On tab did not switch");
            const auto refs = controller.references();
            verify(refs.size() == 24, "Try-On preset count differs from macOS");
            if (!refs.isEmpty())
                controller.selectReference(refs.first().toMap()["id"].toString());
            click("mode_vibex");
            verify(controller.references().size() == 12, "Style preset count differs from macOS");
            click("mode_free");
            auto editor = window->findChild<QQuickItem*>("promptEditor");
            editor->setProperty("text", QStringLiteral("电影质感 cinematic light"));
            click("submitButton");
            verify(controller.prompt.contains("cinematic"), "Free Prompt edit did not bind");
            click("statisticsButton");
            verify(!controller.statisticsVisible, "Statistics toggle did not hide panels");
            click("statisticsButton");
            click("interpolationButton");
            verify(!controller.error.isEmpty(), "Unsupported interpolation should report capability");
            controller.dismissError();
            click("mode_charx");
            window->contentItem()->forceActiveFocus();
            QTest::qWait(2000);
            if (!dir.isEmpty())
                verify(window->grabWindow().save(dir + "/session.png"), "Cannot capture session page");
            click("stopButton");
            verify(!controller.active && !controller.busy, "Stop Session should return to landing page");
            qInfo() << (ok ? "X-Lab UI smoke test passed" : "X-Lab UI smoke test failed");
            app.exit(ok ? 0 : 3);
        });
#else
    if (smoke)
        return 4;
#endif
    return app.exec();
}
