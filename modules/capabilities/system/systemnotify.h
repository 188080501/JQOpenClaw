#ifndef JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMNOTIFY_H_
#define JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMNOTIFY_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class SystemNotify
{
public:
    static bool execute(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error,
        bool *invalidParams
    );
};

#endif // JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMNOTIFY_H_

