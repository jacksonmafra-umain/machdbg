#include "ImageTextBrowser.h"

#include <QScrollBar>
#include <QStyle>

int ImageTextBrowser::imageMaxWidth() const
{
    auto maxWidth = viewport()->width() - qRound(document()->documentMargin() * 2);

    // QTextBrowser decides whether the vertical scroll bar is needed after the
    // document is laid out. Reserve the width up front so wide images do not
    // end up a few pixels too large once the scroll bar appears.
    if(verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff && !verticalScrollBar()->isVisible())
    {
        maxWidth -= style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    }

    // Leave a tiny safety margin for layout rounding.
    return qMax(1, maxWidth - 2);
}

ImageTextBrowser::ImageTextBrowser(QWidget* parent)
    : QTextBrowser(parent)
    , mResizeTimer(new QTimer(this))
{
    mResizeTimer->setSingleShot(true);
    mResizeTimer->setInterval(300);
    connect(mResizeTimer, &QTimer::timeout, this, [this]()
    {
        setText(toHtml());
        auto vbar = verticalScrollBar();
        vbar->setValue(vbar->maximum() * mSavedScrollPercentage);
    });
}

void ImageTextBrowser::resizeImages()
{
    auto vbar = verticalScrollBar();
    auto max = vbar->maximum();
    mSavedScrollPercentage = (max > 0) ? (qreal)vbar->value() / max : 0.0;
    mResizeTimer->start();
}

QVariant ImageTextBrowser::loadResource(int type, const QUrl & name)
{
    QImage image;
    auto url = name.toString();
    if(url.startsWith("http"))
    {
        auto itr = mImageCache.find(url);
        if(itr != mImageCache.end())
        {
            image = itr.value();
        }
        else if(mDownloadFn)
        {
            image = mDownloadFn(url);
            mImageCache.insert(url, image);
        }
    }
    else if(url.startsWith("data:"))
    {
        auto base64Index = url.indexOf(";base64,");
        if(base64Index != -1)
        {
            auto data = QByteArray::fromBase64(url.mid(base64Index + 8).toUtf8());
            image = QImage::fromData(data);
        }
    }
    else
    {
        return QTextBrowser::loadResource(type, name);
    }

    // Scale the image to the width of the document. Reserve room for the
    // vertical scroll bar because it may appear only after layout.
    auto maxWidth = imageMaxWidth();
    if(image.width() > maxWidth)
    {
        image = image.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    }

    return image;
}
