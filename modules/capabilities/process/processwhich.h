#ifndef JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSWHICH_H_
#define JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSWHICH_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class ProcessWhich
{
public:
    static bool execute(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error,
        bool *invalidParams
    );
};

#endif // JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSWHICH_H_

