#ifndef JQOPENCLAW_NODE_NODEOPTIONS_H_
#define JQOPENCLAW_NODE_NODEOPTIONS_H_

// Qt lib import
#include <QtGlobal>
#include <QString>

struct NodeOptions
{
    QString host;
    quint16 port = 0;
    QString token;
    bool tls = false;
    QString tlsFingerprint;
    QString displayName;
    QString nodeId;
    QString configPath;
    QString identityPath;
    QString fileServerUri;
    QString fileServerToken;
    QString deviceFamily = "windows-pc";
    QString modelIdentifier = "JQOpenClawNode";
    bool exitAfterRegister = false;
};

#endif // JQOPENCLAW_NODE_NODEOPTIONS_H_
