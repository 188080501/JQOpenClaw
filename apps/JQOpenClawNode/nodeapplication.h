#ifndef JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_
#define JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_

// Qt lib import
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

// JQOpenClaw import
#include "crypto/deviceidentity/deviceidentity.h"
#include "openclawprotocol/gatewayclient.h"
#include "openclawprotocol/noderegistrar.h"
#include "openclawprotocol/nodeoptions.h"

class QAction;
class QLabel;
class QMenu;
class QWidgetAction;

class NodeApplication : public QObject
{
    Q_OBJECT

public:
    explicit NodeApplication(const NodeOptions &options, QObject *parent = nullptr);

    void start();

signals:
    void finished(int exitCode);

private:
    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Pairing,
        Connected,
        Error
    };

    void initializeSystemTray();
    void updateConnectionStatusAction();
    QString connectionStateDisplayText() const;
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onMainWindowActionTriggered();
    void onExitActionTriggered();

    void onChallengeReceived(const QString &nonce);
    void onConnectAccepted(const QJsonObject &payload);
    void onConnectRejected(const QJsonObject &error);
    void onInvokeRequestReceived(const QJsonObject &payload);
    void onTransportError(const QString &message);
    void onGatewayClosed();
    void onPairingReconnectTimeout();
    void startPairingReconnect();
    void stopPairingReconnect();

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

    struct InvokeReplayTarget
    {
        QString invokeId;
        QString nodeId;
    };

    struct InvokeIdempotencyEntry
    {
        QString requestFingerprint;
        bool completed = false;
        bool ok = false;
        QJsonValue payload;
        QString errorCode;
        QString errorMessage;
        qint64 updatedAtMs = 0;
        QList<InvokeReplayTarget> waitingTargets;
    };

    NodeOptions options_;
    DeviceIdentity identity_;
    GatewayClient gatewayClient_;
    NodeRegistrar registrar_;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QAction *mainWindowAction_ = nullptr;
    QWidgetAction *connectionStatusAction_ = nullptr;
    QLabel *connectionStatusLabel_ = nullptr;
    QAction *exitAction_ = nullptr;
    bool registered_ = false;
    ConnectionState connectionState_ = ConnectionState::Disconnected;
    QString connectionStateDetail_;
    QTimer pairingReconnectTimer_;
    QHash<QString, InvokeIdempotencyEntry> invokeIdempotencyCache_;
    QStringList invokeIdempotencyCacheOrder_;
};

#endif // JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_
