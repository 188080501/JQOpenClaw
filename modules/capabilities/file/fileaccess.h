#ifndef JQOPENCLAW_CAPABILITIES_FILE_FILEACCESS_H_
#define JQOPENCLAW_CAPABILITIES_FILE_FILEACCESS_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class FileAccess
{
public:
    static bool read(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error
    );
    static bool write(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error
    );
};

#endif // JQOPENCLAW_CAPABILITIES_FILE_FILEACCESS_H_

