#pragma once

#include <QMap>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>

#include <functional>

class ImageTextBrowser : public QTextBrowser
{
    Q_OBJECT
public:
    explicit ImageTextBrowser(QWidget* parent = nullptr);
    void resizeImages();

    using DownloadFn = std::function<QImage(const QString &)>;

    void setDownloadFn(DownloadFn fn)
    {
        mDownloadFn = std::move(fn);
    }

protected:
    QVariant loadResource(int type, const QUrl & name) override;

private:
    int imageMaxWidth() const;

    qreal mSavedScrollPercentage = 0.0;
    QTimer* mResizeTimer = nullptr;
    DownloadFn mDownloadFn;
    QMap<QString, QImage> mImageCache;
};
