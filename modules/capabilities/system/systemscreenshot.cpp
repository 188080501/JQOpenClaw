// .h include
#include "capabilities/system/systemscreenshot.h"

// Qt lib import
#include <QBuffer>
#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QtGlobal>

namespace
{
bool captureScreenToJpg(
    QScreen *screen,
    QByteArray *jpgBytes,
    QSize *scaledSize,
    QString *error
)
{
    if ( screen == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("screen is unavailable");
        }
        return false;
    }

    if ( jpgBytes == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("screenshot output bytes pointer is null");
        }
        return false;
    }

    const QPixmap pixmap = screen->grabWindow(0);
    if ( pixmap.isNull() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to capture screen");
        }
        return false;
    }

    QImage image = pixmap.toImage();

    QByteArray encodedJpg;
    QBuffer buffer(&encodedJpg);
    if ( !buffer.open(QIODevice::WriteOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to open screenshot memory buffer");
        }
        return false;
    }

    if ( !image.save(&buffer, "JPG", 90) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to encode screenshot as jpg");
        }
        return false;
    }

    *jpgBytes = encodedJpg;
    if ( scaledSize != nullptr )
    {
        *scaledSize = image.size();
    }
    return true;
}
}

bool SystemScreenshot::captureToJpg(QByteArray *jpgBytes, QSize *scaledSize, QString *error)
{
    qInfo().noquote() << QStringLiteral("[capability.system.screenshot] capture start");

    QScreen *screen = QGuiApplication::primaryScreen();
    if ( screen == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("primary screen is unavailable");
        }
        qWarning().noquote() << QStringLiteral("[capability.system.screenshot] capture failed: primary screen is unavailable");
        return false;
    }

    QString captureError;
    if ( !captureScreenToJpg(screen, jpgBytes, scaledSize, &captureError) )
    {
        if ( error != nullptr )
        {
            *error = captureError;
        }
        qWarning().noquote() << QStringLiteral("[capability.system.screenshot] capture failed: %1").arg(captureError);
        return false;
    }

    qInfo().noquote() << QStringLiteral("[capability.system.screenshot] capture done size=%1x%2 bytes=%3")
                             .arg(scaledSize == nullptr ? -1 : scaledSize->width())
                             .arg(scaledSize == nullptr ? -1 : scaledSize->height())
                             .arg(jpgBytes->size());
    return true;
}

bool SystemScreenshot::captureAllToJpg(QList<CaptureResult> *results, QString *error)
{
    qInfo().noquote() << QStringLiteral("[capability.system.screenshot] capture all screens start");
    if ( results == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("screenshot results output pointer is null");
        }
        qWarning().noquote() << QStringLiteral(
            "[capability.system.screenshot] capture all screens failed: screenshot results output pointer is null"
        );
        return false;
    }

    results->clear();

    const QList< QScreen * > screens = QGuiApplication::screens();
    if ( screens.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("screen list is empty");
        }
        qWarning().noquote() << QStringLiteral("[capability.system.screenshot] capture all screens failed: screen list is empty");
        return false;
    }

    for ( int index = 0; index < screens.size(); ++index )
    {
        QScreen *screen = screens.at(index);
        QByteArray jpgBytes;
        QSize scaledSize;
        QString captureError;
        if ( !captureScreenToJpg(screen, &jpgBytes, &scaledSize, &captureError) )
        {
            qWarning().noquote() << QStringLiteral(
                "[capability.system.screenshot] capture screen skipped index=%1 reason=%2"
            ).arg( QString::number( index ), captureError );
            continue;
        }

        if ( jpgBytes.isEmpty() )
        {
            qWarning().noquote() << QStringLiteral(
                "[capability.system.screenshot] capture screen skipped index=%1 reason=empty image bytes"
            ).arg(index);
            continue;
        }

        CaptureResult captureResult;
        captureResult.screenIndex = index;
        if ( screen != nullptr )
        {
            captureResult.screenName = screen->name();
        }
        captureResult.jpgBytes = jpgBytes;
        captureResult.scaledSize = scaledSize;
        results->append(captureResult);
    }

    if ( results->isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to capture all screens");
        }
        qWarning().noquote() << QStringLiteral("[capability.system.screenshot] capture all screens failed: no screen captured");
        return false;
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.system.screenshot] capture all screens done success=%1 total=%2"
    ).arg(results->size()).arg(screens.size());
    return true;
}
