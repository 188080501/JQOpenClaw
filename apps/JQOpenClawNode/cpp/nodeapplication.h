#ifndef JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_
#define JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_

// Qt lib import
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

// JQOpenClaw import
#include "crypto/deviceidentity/deviceidentity.h"
#include "openclawprotocol/gatewayclient.h"
#include "openclawprotocol/nodeoptions.h"

class QAction;
class QMenu;

class NodeApplication : public QObject
{
    Q_OBJECT

    Q_PROPERTY( ConnectionState connectionState READ connectionState WRITE setConnectionState NOTIFY connectionStateChanged )
    Q_PROPERTY( QString connectionStateText READ connectionStateText NOTIFY connectionStateTextChanged )
    Q_PROPERTY( QString startupTime READ startupTime CONSTANT )
    Q_PROPERTY( QString lastInvokeTime READ lastInvokeTime NOTIFY lastInvokeTimeChanged )
    Q_PROPERTY( QString lastInvokeCapability READ lastInvokeCapability NOTIFY lastInvokeCapabilityChanged )
    Q_PROPERTY( QJsonObject config READ config WRITE setConfig NOTIFY configChanged )

public:
    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Pairing,
        Connected,
        Error
    };
    Q_ENUM( ConnectionState )

    explicit NodeApplication(
        QObject *mainWindowObject = nullptr,
        QObject *parent = nullptr
    );

    Q_INVOKABLE bool saveConfig();
    Q_INVOKABLE bool setFollowSystemStartup(bool enabled);
    Q_INVOKABLE bool setSilentStartup(bool enabled);
    bool silentStartupEnabled() const;
    QString startupTime() const;
    void setMainWindowObject(QObject *mainWindowObject);
    void start();

private:
    void initializeSystemTray();
    void hideTrayIconIfNeeded();
    void requestApplicationExit(int code);
    void showMainWindow();
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
    static bool isPairingRequiredConnectError(const QJsonObject &errorObject);
    static QString parseErrorMessage(const QJsonObject &errorObject);
    static QString generateDefaultDisplayName();
    static QJsonObject defaultConfig();
    static QJsonObject normalizeConfig(const QJsonObject &config);
    static QString defaultIdentityPath();
    bool reconnectGatewayFromConfig(QString *error);
    bool loadConfigFromDisk(QString *error);
    bool saveConfigToDisk(const QJsonObject &config, QString *error) const;
    NodeOptions buildNodeOptions(QString *error) const;
    QString configString(
        const QString &key,
        const QString &defaultValue = QString()
    ) const;

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

    DeviceIdentity identity_;
    GatewayClient gatewayClient_;
    QPointer< QObject > mainWindowObject_;
    QSystemTrayIcon *trayIcon_ = nullptr;
    QMenu *trayMenu_ = nullptr;
    QAction *mainWindowAction_ = nullptr;
    QAction *exitAction_ = nullptr;
    bool registered_ = false;
    QString startupTime_;
    QString connectionStateDetail_;
    QTimer pairingReconnectTimer_;
    QHash<QString, InvokeIdempotencyEntry> invokeIdempotencyCache_;
    QStringList invokeIdempotencyCacheOrder_;
    QString configPath_;
    bool reconnectAfterClose_ = false;
    bool reconnectingFromConfigSave_ = false;

    // Property statement code start
private: ConnectionState connectionState_ = ConnectionState::Disconnected;
public: inline ConnectionState connectionState() const;
public: inline void setConnectionState(const ConnectionState &newValue);
    Q_SIGNAL void connectionStateChanged(const ConnectionState connectionState);
private: QString connectionStateText_ = QStringLiteral("未连接");
public: inline QString connectionStateText() const;
    Q_SIGNAL void connectionStateTextChanged(const QString connectionStateText);

private: QString lastInvokeTime_ = QStringLiteral("无");
public: inline QString lastInvokeTime() const;
public: inline void setLastInvokeTime(const QString &newValue);
    Q_SIGNAL void lastInvokeTimeChanged(const QString lastInvokeTime);
private: QString lastInvokeCapability_ = QString();
public: inline QString lastInvokeCapability() const;
public: inline void setLastInvokeCapability(const QString &newValue);
    Q_SIGNAL void lastInvokeCapabilityChanged(const QString lastInvokeCapability);

private: QJsonObject config_;
public: inline QJsonObject config() const;
public: inline void setConfig(const QJsonObject &newValue);
    Q_SIGNAL void configChanged(const QJsonObject config);
private:
    // Property statement code end
};

// .inc include
#include "nodeapplication.inc"

#endif // JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_H_
