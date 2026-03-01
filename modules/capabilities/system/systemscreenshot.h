#ifndef JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMSCREENSHOT_H_
#define JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMSCREENSHOT_H_

// Qt lib import
#include <QByteArray>
#include <QList>
#include <QSize>
#include <QString>

class SystemScreenshot
{
public:
    struct CaptureResult
    {
        int screenIndex = -1;
        QString screenName;
        QByteArray jpgBytes;
        QSize scaledSize;
    };

    static bool captureToJpg(QByteArray *jpgBytes, QSize *scaledSize, QString *error);
    static bool captureAllToJpg(QList<CaptureResult> *results, QString *error);
};

#endif // JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMSCREENSHOT_H_
