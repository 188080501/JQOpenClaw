#ifndef JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_
#define JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_

// Qt lib import
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

// JQOpenClaw import
#include "crypto/deviceidentity/deviceidentity.h"
#include "openclawprotocol/gatewayclient.h"
#include "openclawprotocol/noderegistrar.h"
#include "openclawprotocol/nodeoptions.h"

class NodeApplication : public QObject
{
    Q_OBJECT

public:
    explicit NodeApplication(const NodeOptions &options, QObject *parent = nullptr);

    void start();

signals:
    void finished(int exitCode);

private:
    void onChallengeReceived(const QString &nonce);
    void onConnectAccepted(const QJsonObject &payload);
    void onConnectRejected(const QJsonObject &error);
    void onInvokeRequestReceived(const QJsonObject &payload);
    void onTransportError(const QString &message);
    void onGatewayClosed();

    bool runCryptoSelfTest(QString *error) const;
    bool parseInvokeParamsJson(
        const QJsonValue &paramsJsonValue,
        QJsonValue *params,
        QString *error
    ) const;
    bool executeInvokeCommand(
        const QString &command,
        const QJsonValue &params,
        int invokeTimeoutMs,
        QJsonValue *payload,
        QString *errorCode,
        QString *errorMessage
    ) const;
    void sendInvokeSuccess(const QString &invokeId, const QString &nodeId, const QJsonValue &payload);
    void sendInvokeError(
        const QString &invokeId,
        const QString &nodeId,
        const QString &code,
        const QString &message
    );
    void sendConnectRequest(const QString &nonce);
    static QString parseErrorMessage(const QJsonObject &errorObject);

    NodeOptions options_;
    DeviceIdentity identity_;
    GatewayClient gatewayClient_;
    NodeRegistrar registrar_;
    bool registered_ = false;
};

#endif // JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_
