#ifndef JQOPENCLAW_CAPABILITIES_FILE_FILEACCESSWRITE_H_
#define JQOPENCLAW_CAPABILITIES_FILE_FILEACCESSWRITE_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class FileWriteAccess
{
public:
    static bool write(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error,
        bool *invalidParams
    );
};

#endif // JQOPENCLAW_CAPABILITIES_FILE_FILEACCESSWRITE_H_
