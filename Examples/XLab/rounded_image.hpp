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
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
  public:
    enum Status { Null, Loading, Ready, Error };
    Q_ENUM(Status)
    explicit RoundedImage(QQuickItem* parent = nullptr);
    ~RoundedImage() override;
    QUrl source() const { return source_; }
    Status status() const { return status_; }
    QString errorString() const { return error_; }
    void setSource(const QUrl&);
    Q_INVOKABLE void retry();
    void paint(QPainter*) override;
  signals:
    void sourceChanged();
    void statusChanged();

  private:
    QUrl source_;
    QImage image_;
    QPointer<QNetworkReply> reply_;
    Status status_ = Null;
    QString error_;
    void accept(const QByteArray&);
    void load();
    void setStatus(Status, const QString& error = {});
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
