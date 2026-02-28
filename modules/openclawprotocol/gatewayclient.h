#ifndef JQOPENCLAW_GATEWAY_GATEWAYCLIENT_H_
#define JQOPENCLAW_GATEWAY_GATEWAYCLIENT_H_

// Qt lib import
#include <QAbstractSocket>
#include <QJsonObject>
#include <QObject>
#include <QSslError>
#include <QWebSocket>

// JQOpenClaw import
#include "openclawprotocol/nodeoptions.h"

class GatewayClient : public QObject
{
    Q_OBJECT

public:
    explicit GatewayClient(QObject *parent = nullptr);

    void setOptions(const NodeOptions &options);
    void open();
    void close();
    bool isOpen() const;
    void sendConnect(const QJsonObject &params);
    void sendInvokeResult(const QJsonObject &params);

signals:
    void opened();
    void closed();
    void challengeReceived(const QString &nonce);
    void invokeRequestReceived(const QJsonObject &payload);
    void connectAccepted(const QJsonObject &payload);
    void connectRejected(const QJsonObject &error);
    void transportError(const QString &message);

private:
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);
    void onTextMessageReceived(const QString &message);
    void onSslErrors(const QList<QSslError> &errors);

    QString gatewayUrl() const;

    NodeOptions options_;
    QWebSocket socket_;
    QString pendingConnectRequestId_;
};

#endif // JQOPENCLAW_GATEWAY_GATEWAYCLIENT_H_
