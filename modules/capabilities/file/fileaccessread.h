#ifndef JQOPENCLAW_CAPABILITIES_FILE_FILEACCESSREAD_H_
#define JQOPENCLAW_CAPABILITIES_FILE_FILEACCESSREAD_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class FileReadAccess
{
public:
    static bool read(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error
    );
};

#endif // JQOPENCLAW_CAPABILITIES_FILE_FILEACCESSREAD_H_
