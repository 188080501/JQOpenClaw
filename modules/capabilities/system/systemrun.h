#ifndef JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMRUN_H_
#define JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMRUN_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class SystemRun
{
public:
    static bool execute(
        const QJsonValue &params,
        int invokeTimeoutMs,
        QJsonObject *result,
        QString *error,
        bool *invalidParams
    );
};

#endif // JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMRUN_H_
