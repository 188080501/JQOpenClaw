// .h include
#include "capabilities/screenshot/screenshotcapture.h"

// Qt lib import
#include <QBuffer>
#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QtGlobal>

bool ScreenshotCapture::captureToJpg(QByteArray *jpgBytes, QSize *scaledSize, QString *error)
{
    qInfo().noquote() << QStringLiteral("[capability.screenshot] capture start");
    if ( jpgBytes == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("screenshot output bytes pointer is null");
        }
        qWarning().noquote() << QStringLiteral("[capability.screenshot] capture failed: screenshot output bytes pointer is null");
        return false;
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if ( screen == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("primary screen is unavailable");
        }
        qWarning().noquote() << QStringLiteral("[capability.screenshot] capture failed: primary screen is unavailable");
        return false;
    }

    const QPixmap pixmap = screen->grabWindow(0);
    if ( pixmap.isNull() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to capture screen");
        }
        qWarning().noquote() << QStringLiteral("[capability.screenshot] capture failed: failed to capture screen");
        return false;
    }

    QImage image = pixmap.toImage().scaledToWidth( 640, Qt::SmoothTransformation );

    QByteArray encodedJpg;
    QBuffer buffer(&encodedJpg);
    if ( !buffer.open(QIODevice::WriteOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to open screenshot memory buffer");
        }
        qWarning().noquote() << QStringLiteral("[capability.screenshot] capture failed: failed to open screenshot memory buffer");
        return false;
    }

    if ( !image.save( &buffer, "JPG", 90 ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to encode screenshot as jpg");
        }
        qWarning().noquote() << QStringLiteral("[capability.screenshot] capture failed: failed to encode screenshot as jpg");
        return false;
    }

    *jpgBytes = encodedJpg;
    if ( scaledSize != nullptr )
    {
        *scaledSize = image.size();
    }
    qInfo().noquote() << QStringLiteral("[capability.screenshot] capture done size=%1x%2 bytes=%3")
                             .arg(image.width())
                             .arg(image.height())
                             .arg(jpgBytes->size());
    return true;
}
