#ifndef JQOPENCLAW_CAPABILITIES_SCREENSHOT_SCREENSHOTCAPTURE_H_
#define JQOPENCLAW_CAPABILITIES_SCREENSHOT_SCREENSHOTCAPTURE_H_

// Qt lib import
#include <QByteArray>
#include <QSize>
#include <QString>

class ScreenshotCapture
{
public:
    static bool captureToJpg(QByteArray *jpgBytes, QSize *scaledSize, QString *error);
};

#endif // JQOPENCLAW_CAPABILITIES_SCREENSHOT_SCREENSHOTCAPTURE_H_
