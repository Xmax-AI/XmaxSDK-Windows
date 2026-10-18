#pragma once
#include <QImage>
#include <QNetworkReply>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
// QPainter clipping also works with the software scene graph used by UI tests.
class RoundedImage : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
  public:
    explicit RoundedImage(QQuickItem* parent = nullptr);
    ~RoundedImage() override;
    QUrl source() const { return source_; }
    void setSource(const QUrl&);
    void paint(QPainter*) override;
  signals:
    void sourceChanged();

  private:
    QUrl source_;
    QImage image_;
    QPointer<QNetworkReply> reply_;
    void accept(const QByteArray&);
};

class CheckerboardSurface : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
  public:
    explicit CheckerboardSurface(QQuickItem* parent = nullptr) : QQuickPaintedItem(parent) {
        setAntialiasing(true);
    }
    void paint(QPainter*) override;
};
