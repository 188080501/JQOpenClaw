#ifndef JQOPENCLAW_NODE_NODEPROFILE_H_
#define JQOPENCLAW_NODE_NODEPROFILE_H_

// Qt lib import
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace NodeProfile
{
int minProtocolVersion();
int maxProtocolVersion();

QString clientId();
QString clientVersion();

QJsonArray caps();
QJsonArray commands();
QJsonObject permissions();
}

#endif // JQOPENCLAW_NODE_NODEPROFILE_H_
