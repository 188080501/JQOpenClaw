#ifndef JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSMANAGE_H_
#define JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSMANAGE_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class ProcessManage
{
public:
    static bool execute(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error,
        bool *invalidParams
    );
};

#endif // JQOPENCLAW_CAPABILITIES_PROCESS_PROCESSMANAGE_H_
