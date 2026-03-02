#ifndef JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSEXEC_H_
#define JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSEXEC_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class ProcessExec
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

#endif // JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSEXEC_H_
