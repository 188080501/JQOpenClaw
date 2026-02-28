#ifndef JQOPENCLAW_NODE_NODEREGISTRAR_H_
#define JQOPENCLAW_NODE_NODEREGISTRAR_H_

// Qt lib import
#include <QJsonObject>
#include <QString>

// JQOpenClaw import
#include "crypto/deviceidentity/deviceidentity.h"
#include "openclawprotocol/nodeoptions.h"

class NodeRegistrar
{
public:
    explicit NodeRegistrar(const NodeOptions &options);

    bool buildConnectParams(
        const DeviceIdentity &identity,
        const QString &challengeNonce,
        QJsonObject *params,
        QString *error
    ) const;

private:
    static QString platformName();

    NodeOptions options_;
};

#endif // JQOPENCLAW_NODE_NODEREGISTRAR_H_
