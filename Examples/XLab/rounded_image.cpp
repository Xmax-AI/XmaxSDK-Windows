#include "rounded_image.hpp"
#include <QBuffer>
#include <QCache>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
namespace {
QCache<QString, QImage> cache(16384); // At most 16 MiB of decoded thumbnails.
QNetworkAccessManager* manager() {
    static auto value = new QNetworkAccessManager(qApp);
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
    if (data.size() > 8 * 1024 * 1024)
        return;
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.isValid())
        reader.setScaledSize(size.scaled(160, 160, Qt::KeepAspectRatio));
    image_ = reader.read();
    if (!image_.isNull())
        cache.insert(source_.toString(), new QImage(image_), std::max(1, int(image_.sizeInBytes() / 1024)));
    update();
}
void RoundedImage::setSource(const QUrl& url) {
    if (source_ == url)
        return;
    source_ = url;
    image_ = {};
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    emit sourceChanged();
    update();
    if (url.isEmpty())
        return;
    if (auto found = cache.object(url.toString())) {
        image_ = *found;
        update();
        return;
    }
    if (url.isLocalFile()) {
        QFile f(url.toLocalFile());
        if (f.open(QFile::ReadOnly) && f.size() <= 20 * 1024 * 1024)
            accept(f.readAll());
        return;
    }
    if (url.scheme() != "https")
        return;
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    reply_ = manager()->get(request);
    auto reply = reply_;
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > 8 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        if (url == source_ && reply->error() == QNetworkReply::NoError)
            accept(reply->readAll());
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
