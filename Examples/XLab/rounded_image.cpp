#include "rounded_image.hpp"
#include <QBuffer>
#include <QCache>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
namespace {
constexpr qint64 maxImageBytes = 8 * 1024 * 1024;
QCache<QString, QImage> cache(16384); // At most 16 MiB of decoded thumbnails.
QNetworkAccessManager* manager() {
    static auto value = [] {
        auto network = new QNetworkAccessManager(qApp);
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!directory.isEmpty()) {
            auto disk = new QNetworkDiskCache(network);
            disk->setCacheDirectory(QDir(directory).filePath("thumbnails"));
            disk->setMaximumCacheSize(32 * 1024 * 1024);
            network->setCache(disk);
        }
        return network;
    }();
    return value;
}
} // namespace
RoundedImage::RoundedImage(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}
RoundedImage::~RoundedImage() {
    if (reply_)
        reply_->abort();
}
void RoundedImage::accept(const QByteArray& data) {
    if (data.size() > maxImageBytes) {
        setStatus(Error, QStringLiteral("Thumbnail exceeds the 8 MiB limit. Click to retry."));
        return;
    }
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.isValid())
        reader.setScaledSize(size.scaled(160, 160, Qt::KeepAspectRatio));
    image_ = reader.read();
    if (image_.isNull()) {
        setStatus(Error,
                  QStringLiteral("Cannot decode thumbnail: %1. Click to retry.").arg(reader.errorString()));
        return;
    }
    cache.insert(source_.toString(), new QImage(image_), std::max(1, int(image_.sizeInBytes() / 1024)));
    setStatus(Ready);
    update();
}
void RoundedImage::setStatus(Status status, const QString& error) {
    if (status_ == status && error_ == error)
        return;
    status_ = status;
    error_ = error;
    emit statusChanged();
    update();
}
void RoundedImage::setSource(const QUrl& url) {
    if (source_ == url)
        return;
    source_ = url;
    emit sourceChanged();
    load();
}
void RoundedImage::retry() {
    if (status_ == Error)
        load();
}
void RoundedImage::load() {
    const auto url = source_;
    image_ = {};
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    setStatus(Null);
    update();
    if (url.isEmpty())
        return;
    if (auto found = cache.object(url.toString())) {
        image_ = *found;
        setStatus(Ready);
        update();
        return;
    }
    setStatus(Loading);
    if (url.isLocalFile() || url.scheme() == "qrc") {
        QFile f(url.isLocalFile() ? url.toLocalFile() : ":" + url.path());
        if (!f.open(QFile::ReadOnly))
            setStatus(Error, QStringLiteral("Cannot open thumbnail. Click to retry."));
        else if (f.size() > maxImageBytes)
            setStatus(Error, QStringLiteral("Thumbnail exceeds the 8 MiB limit. Click to retry."));
        else
            accept(f.read(maxImageBytes + 1));
        return;
    }
    if (url.scheme() != "https") {
        setStatus(Error, QStringLiteral("Thumbnail must use HTTPS or a local file."));
        return;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, true);
    reply_ = manager()->get(request);
    auto reply = reply_;
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        if (received > maxImageBytes || total > maxImageBytes) {
            setStatus(Error, QStringLiteral("Thumbnail exceeds the 8 MiB limit. Click to retry."));
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        if (url == source_ && status_ != Error) {
            if (reply->error() == QNetworkReply::NoError)
                accept(reply->readAll());
            else
                setStatus(Error, QStringLiteral("Thumbnail could not load: %1. Click to retry.")
                                     .arg(reply->errorString()));
        }
        if (reply_ == reply)
            reply_ = nullptr;
        reply->deleteLater();
    });
}
void RoundedImage::paint(QPainter* painter) {
    if (image_.isNull())
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addEllipse(boundingRect());
    painter->setClipPath(path);
    const QSizeF size =
        QSizeF(image_.size()).scaled(QSizeF(width(), height()), Qt::KeepAspectRatioByExpanding);
    painter->drawImage(
        QRectF((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()),
        image_);
}

void CheckerboardSurface::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(boundingRect(), 24, 24);
    painter->setClipPath(path);
    for (int row = 0; row * 38 < height(); ++row)
        for (int column = 0; column * 38 < width(); ++column)
            painter->fillRect(QRectF(column * 38, row * 38, 38, 38),
                              (row + column) % 2 == 0 ? QColor("#434347") : QColor("#37373b"));
}
