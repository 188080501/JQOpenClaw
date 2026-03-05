#ifndef JQOPENCLAW_CAPABILITIES_NODE_NODESELFUPDATE_H_
#define JQOPENCLAW_CAPABILITIES_NODE_NODESELFUPDATE_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class NodeSelfUpdate
{
public:
    static bool execute(
        const QJsonValue &params,
        QJsonObject *result,
        QString *error,
        bool *invalidParams,
        bool *md5Mismatch
    );
};

#endif // JQOPENCLAW_CAPABILITIES_NODE_NODESELFUPDATE_H_
