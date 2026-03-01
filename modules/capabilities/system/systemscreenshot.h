#ifndef JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMSCREENSHOT_H_
#define JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMSCREENSHOT_H_

// Qt lib import
#include <QByteArray>
#include <QSize>
#include <QString>

class SystemScreenshot
{
public:
    static bool captureToJpg(QByteArray *jpgBytes, QSize *scaledSize, QString *error);
};

#endif // JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMSCREENSHOT_H_
