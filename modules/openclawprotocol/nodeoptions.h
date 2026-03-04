#ifndef JQOPENCLAW_NODE_NODEOPTIONS_H_
#define JQOPENCLAW_NODE_NODEOPTIONS_H_

// Qt lib import
#include <QJsonObject>
#include <QString>

struct NodeOptions
{
    QString gatewayUrl;
    QString token;
    bool tls = false;
    QString displayName;
    QString nodeId;
    QString configPath;
    QString identityPath;
    QString fileServerUrl;
    QString fileServerToken;
    QString deviceFamily = "windows-pc";
    QString modelIdentifier = "JQOpenClawNode";
    QJsonObject commandPermissions;
    bool exitAfterRegister = false;
};

#endif // JQOPENCLAW_NODE_NODEOPTIONS_H_
