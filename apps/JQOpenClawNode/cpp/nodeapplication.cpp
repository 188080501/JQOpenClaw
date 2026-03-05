// .h include
#include "nodeapplication.h"

// Qt lib import
#include <limits>
#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

// JQOpenClaw import
#include "capabilities/file/fileaccessread.h"
#include "capabilities/file/fileaccesswrite.h"
#include "capabilities/node/nodeselfupdate.h"
#include "capabilities/process/processmanage.h"
#include "capabilities/process/processexec.h"
#include "capabilities/process/processwhich.h"
#include "capabilities/system/systemscreenshot.h"
#include "capabilities/system/systeminfo.h"
#include "capabilities/system/systemnotify.h"
#include "capabilities/system/systemclipboard.h"
#include "capabilities/system/systeminput.h"
#include "crypto/secretbox/secretboxcrypto.h"
#include "crypto/signing/deviceauth.h"
#include "openclawprotocol/noderegistrar.h"
#include "openclawprotocol/nodeprofile.h"

namespace
{
constexpr int kPairingReconnectIntervalMs = 15000;
constexpr int kInvokeIdempotencyCacheMaxEntries = 256;
constexpr qint64 kInvokeIdempotencyCacheTtlMs = 10LL * 60LL * 1000LL;
constexpr int kInvokeHistoryMaxEntries = 10;
constexpr int kScreenshotUploadTimeoutMs = 30000;
// Internal-only delay before process exit after self-update response is sent.
constexpr int kSelfUpdateExitDelayMs = 200;
constexpr auto kDefaultGatewayUrl = "ws://127.0.0.1:18789";

QString startupCommandLine()
{
    const QString appPath = QCoreApplication::applicationFilePath().trimmed();
    if ( appPath.isEmpty() )
    {
        return QString();
    }

    return QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(appPath));
}

bool applyFollowSystemStartupConfig(
    const QJsonObject &config,
    QString *error
)
{
#if defined(Q_OS_WIN)
    const bool enabled = config.value(QStringLiteral("followSystemStartup")).toBool(false);
    QSettings startupSettings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat
    );

    const QString startupValueName = QStringLiteral("JQOpenClawNode");
    if ( enabled )
    {
        const QString command = startupCommandLine();
        if ( command.isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("failed to resolve app path for startup command");
            }
            return false;
        }
        startupSettings.setValue(startupValueName, command);
    }
    else
    {
        startupSettings.remove(startupValueName);
    }

    startupSettings.sync();
    if ( startupSettings.status() != QSettings::NoError )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to update startup setting in registry (status=%1)"
            ).arg(static_cast<int>(startupSettings.status()));
        }
        return false;
    }

    return true;
#else
    Q_UNUSED(config);
    if ( error != nullptr )
    {
        error->clear();
    }
    return true;
#endif
}

QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

bool trySerializeJsonValue(const QJsonValue &value, QString *json)
{
    if ( json == nullptr )
    {
        return false;
    }

    if ( value.isUndefined() )
    {
        return false;
    }

    if ( value.isObject() )
    {
        *json = QString::fromUtf8(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)
        );
        return true;
    }

    if ( value.isArray() )
    {
        *json = QString::fromUtf8(
            QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
        );
        return true;
    }

    if ( value.isNull() )
    {
        *json = QStringLiteral("null");
        return true;
    }

    if ( value.isBool() )
    {
        *json = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        return true;
    }

    if ( value.isDouble() )
    {
        *json = QString::number(value.toDouble(), 'g', 16);
        return true;
    }

    if ( value.isString() )
    {
        QJsonArray wrapper;
        wrapper.append(value);
        const QByteArray wrappedJson = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
        if ( wrappedJson.size() < 3 )
        {
            return false;
        }
        *json = QString::fromUtf8(
            wrappedJson.mid(1, wrappedJson.size() - 2)
        );
        return true;
    }

    return false;
}

QString buildInvokeIdempotencyCacheKey(
    const QString &nodeId,
    const QString &command,
    const QString &idempotencyKey
)
{
    return QStringLiteral("%1\n%2\n%3").arg(nodeId, command, idempotencyKey);
}

QString buildInvokeRequestFingerprint(
    const QString &command,
    const QJsonValue &params,
    int invokeTimeoutMs
)
{
    QString paramsJson;
    if ( !trySerializeJsonValue(params, &paramsJson) )
    {
        paramsJson = QStringLiteral("null");
    }

    return QStringLiteral("%1\n%2\n%3")
        .arg(command, QString::number(invokeTimeoutMs), paramsJson);
}

QString normalizeBasePath(const QString &path)
{
    QString normalizedPath = path.trimmed();
    if ( normalizedPath == QStringLiteral("/") )
    {
        normalizedPath.clear();
    }

    while ( normalizedPath.endsWith('/') )
    {
        normalizedPath.chop(1);
    }

    if ( normalizedPath.isEmpty() )
    {
        return QString();
    }

    if ( !normalizedPath.startsWith('/') )
    {
        normalizedPath.prepend('/');
    }
    return normalizedPath;
}

QUrl buildFileServerUrl(
    const QString &baseUrl,
    const QString &segment,
    const QString &fileName
)
{
    QUrl url(baseUrl.trimmed());
    const QString basePath = normalizeBasePath(url.path());
    const QString path = QStringLiteral("%1/%2/%3")
        .arg(basePath, segment, fileName);
    url.setPath(path);
    return url;
}

QString generateScreenshotFileName()
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmsszzz");
    const QString randomId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QStringLiteral("screenshot-%1-%2.jpg").arg(timestamp, randomId);
}

bool uploadScreenshotFile(
    const QByteArray &imageBytes,
    const QString &fileServerUrl,
    const QString &fileServerToken,
    QString *fileUrl,
    QString *error
)
{
    if ( fileUrl == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file url output pointer is null");
        }
        return false;
    }

    const QString normalizedUrl = fileServerUrl.trimmed();
    if ( normalizedUrl.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file server url is empty");
        }
        return false;
    }

    const QString normalizedToken = fileServerToken.trimmed();
    if ( normalizedToken.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file server token is empty");
        }
        return false;
    }

    const QString fileName = generateScreenshotFileName();
    const QUrl uploadUrl = buildFileServerUrl(normalizedUrl, QStringLiteral("upload"), fileName);
    if ( !uploadUrl.isValid() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid file server upload url");
        }
        return false;
    }

    QNetworkRequest request(uploadUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
    request.setRawHeader("X-Token", normalizedToken.toUtf8());

    QNetworkAccessManager networkAccessManager;
    QNetworkReply *reply = networkAccessManager.put(request, imageBytes);

    QEventLoop eventLoop;
    bool requestTimedOut = false;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(
        &timeoutTimer,
        &QTimer::timeout,
        &eventLoop,
        [ &eventLoop, reply, &requestTimedOut ]()
        {
            requestTimedOut = true;
            if ( reply != nullptr )
            {
                reply->abort();
            }
            eventLoop.quit();
        }
    );
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    timeoutTimer.start(kScreenshotUploadTimeoutMs);
    eventLoop.exec();
    timeoutTimer.stop();

    if ( requestTimedOut )
    {
        reply->deleteLater();
        if ( error != nullptr )
        {
            *error = QStringLiteral("file upload timed out after %1ms")
                .arg(QString::number(kScreenshotUploadTimeoutMs));
        }
        return false;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray responseBody = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString().trimmed();
    reply->deleteLater();

    if ( networkError != QNetworkReply::NoError )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file upload network error (%1): %2")
                .arg(static_cast<int>(networkError))
                .arg(
                    networkErrorText.isEmpty()
                        ? QStringLiteral("unknown network error")
                        : networkErrorText
                );
        }
        return false;
    }

    if ( ( statusCode < 200 ) ||
         ( statusCode >= 300 ) )
    {
        if ( error != nullptr )
        {
            const QString bodyText = QString::fromUtf8(responseBody).trimmed().left(200);
            if ( bodyText.isEmpty() )
            {
                *error = QStringLiteral("file upload failed with status code %1").arg(statusCode);
            }
            else
            {
                *error = QStringLiteral("file upload failed with status code %1: %2")
                    .arg(statusCode)
                    .arg(bodyText);
            }
        }
        return false;
    }

    const QUrl fileAccessUrl = buildFileServerUrl(normalizedUrl, QStringLiteral("files"), fileName);
    if ( !fileAccessUrl.isValid() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid file server access url");
        }
        return false;
    }

    *fileUrl = fileAccessUrl.toString(QUrl::FullyEncoded);
    return true;
}

bool parseInvokeTimeoutMs(const QJsonObject &payload, int *timeoutMs, QString *error)
{
    if ( timeoutMs == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invoke timeout output pointer is null");
        }
        return false;
    }

    *timeoutMs = -1;
    const QJsonValue timeoutValue = payload.value(QStringLiteral("timeoutMs"));
    if ( timeoutValue.isUndefined() || timeoutValue.isNull() )
    {
        return true;
    }

    if ( !timeoutValue.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("timeoutMs must be non-negative integer");
        }
        return false;
    }

    const double rawTimeoutMs = timeoutValue.toDouble();
    if ( ( rawTimeoutMs < 0.0 ) ||
         ( rawTimeoutMs > static_cast<double>((std::numeric_limits<int>::max)()) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("timeoutMs must be non-negative integer");
        }
        return false;
    }

    const int parsedTimeoutMs = static_cast<int>(rawTimeoutMs);
    if ( rawTimeoutMs != static_cast<double>(parsedTimeoutMs) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("timeoutMs must be non-negative integer");
        }
        return false;
    }

    *timeoutMs = parsedTimeoutMs;
    return true;
}

}

NodeApplication::NodeApplication(
    QObject *mainWindowObject,
    QObject *parent
) :
    QObject(parent),
    gatewayClient_(this),
    mainWindowObject_(mainWindowObject),
    pairingReconnectTimer_(this)
{
    startupTime_ = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    pairingReconnectTimer_.setInterval(kPairingReconnectIntervalMs);
    pairingReconnectTimer_.setSingleShot(false);

    connect(
        &gatewayClient_,
        &GatewayClient::challengeReceived,
        this,
        &NodeApplication::onChallengeReceived
    );
    connect(
        &gatewayClient_,
        &GatewayClient::connectAccepted,
        this,
        &NodeApplication::onConnectAccepted
    );
    connect(
        &gatewayClient_,
        &GatewayClient::connectRejected,
        this,
        &NodeApplication::onConnectRejected
    );
    connect(
        &gatewayClient_,
        &GatewayClient::invokeRequestReceived,
        this,
        &NodeApplication::onInvokeRequestReceived
    );
    connect(
        &gatewayClient_,
        &GatewayClient::transportError,
        this,
        &NodeApplication::onTransportError
    );
    connect(
        &gatewayClient_,
        &GatewayClient::closed,
        this,
        &NodeApplication::onGatewayClosed
    );
    connect(
        &pairingReconnectTimer_,
        &QTimer::timeout,
        this,
        &NodeApplication::onPairingReconnectTimeout
    );
    if ( QCoreApplication::instance() != nullptr )
    {
        connect(
            QCoreApplication::instance(),
            &QCoreApplication::aboutToQuit,
            this,
            &NodeApplication::hideTrayIconIfNeeded
        );
    }

    setConfig(defaultConfig());
    QString loadConfigError;
    const bool configLoaded = loadConfigFromDisk(&loadConfigError);
    if ( !configLoaded )
    {
        qWarning().noquote() << loadConfigError;
        return;
    }

    QString followSystemStartupError;
    if ( !applyFollowSystemStartupConfig(config_, &followSystemStartupError) )
    {
        if ( !followSystemStartupError.trimmed().isEmpty() )
        {
            qWarning().noquote() << followSystemStartupError;
        }
    }
}

void NodeApplication::setMainWindowObject(QObject *mainWindowObject)
{
    mainWindowObject_ = mainWindowObject;
}

QString NodeApplication::startupTime() const
{
    return startupTime_;
}

bool NodeApplication::saveConfig()
{
    QJsonObject normalizedConfig = normalizeConfig(config_);
    if ( normalizedConfig.value(QStringLiteral("displayName")).toString().trimmed().isEmpty() )
    {
        normalizedConfig.insert(QStringLiteral("displayName"), generateDefaultDisplayName());
    }
    if ( normalizedConfig.value(QStringLiteral("instanceId")).toString().trimmed().isEmpty() )
    {
        normalizedConfig.insert(
            QStringLiteral("instanceId"),
            QUuid::createUuid().toString(QUuid::WithoutBraces)
        );
    }

    QString saveError;
    if ( !saveConfigToDisk(normalizedConfig, &saveError) )
    {
        qCritical().noquote() << saveError;
        return false;
    }

    setConfig(normalizedConfig);

    QString followSystemStartupError;
    if ( !applyFollowSystemStartupConfig(normalizedConfig, &followSystemStartupError) )
    {
        if ( !followSystemStartupError.trimmed().isEmpty() )
        {
            qWarning().noquote() << followSystemStartupError;
        }
    }

    QString reconnectError;
    if ( !reconnectGatewayFromConfig(&reconnectError) )
    {
        if ( !reconnectError.trimmed().isEmpty() )
        {
            qWarning().noquote() << reconnectError;
        }
    }

    qInfo().noquote() << QStringLiteral("node config saved: %1").arg(configPath_);
    return true;
}

bool NodeApplication::setFollowSystemStartup(bool enabled)
{
    QJsonObject normalizedConfig = normalizeConfig(config_);
    const bool oldValue = normalizedConfig.value(QStringLiteral("followSystemStartup"))
        .toBool(false);
    if ( oldValue == enabled )
    {
        return true;
    }

    normalizedConfig.insert(QStringLiteral("followSystemStartup"), enabled);

    QString saveError;
    if ( !saveConfigToDisk(normalizedConfig, &saveError) )
    {
        qCritical().noquote() << saveError;
        return false;
    }

    setConfig(normalizedConfig);

    QString followSystemStartupError;
    if ( !applyFollowSystemStartupConfig(normalizedConfig, &followSystemStartupError) )
    {
        if ( !followSystemStartupError.trimmed().isEmpty() )
        {
            qWarning().noquote() << followSystemStartupError;
        }
        return false;
    }

    qInfo().noquote() << QStringLiteral("followSystemStartup updated: %1")
        .arg(enabled ? QStringLiteral("true") : QStringLiteral("false"));
    return true;
}

bool NodeApplication::setSilentStartup(bool enabled)
{
    QJsonObject normalizedConfig = normalizeConfig(config_);
    const bool oldValue = normalizedConfig.value(QStringLiteral("silentStartup"))
        .toBool(false);
    if ( oldValue == enabled )
    {
        return true;
    }

    normalizedConfig.insert(QStringLiteral("silentStartup"), enabled);

    QString saveError;
    if ( !saveConfigToDisk(normalizedConfig, &saveError) )
    {
        qCritical().noquote() << saveError;
        return false;
    }

    setConfig(normalizedConfig);

    qInfo().noquote() << QStringLiteral("silentStartup updated: %1")
        .arg(enabled ? QStringLiteral("true") : QStringLiteral("false"));
    return true;
}

bool NodeApplication::silentStartupEnabled() const
{
    const QJsonObject normalizedConfig = normalizeConfig(config_);
    return normalizedConfig.value(QStringLiteral("silentStartup")).toBool(false);
}

QString NodeApplication::generateDefaultDisplayName()
{
    const uint suffix = QRandomGenerator::global()->bounded(10000U);
    return QStringLiteral("JQOpenClawNode-%1").arg(suffix, 4, 10, QLatin1Char('0'));
}

QJsonObject NodeApplication::defaultConfig()
{
    QJsonObject config;
    config.insert(QStringLiteral("gatewayUrl"), QString::fromLatin1(kDefaultGatewayUrl));
    config.insert(QStringLiteral("token"), QString());
    config.insert(QStringLiteral("followSystemStartup"), false);
    config.insert(QStringLiteral("silentStartup"), false);
    config.insert(QStringLiteral("displayName"), QString());
    config.insert(QStringLiteral("instanceId"), QString());
    config.insert(QStringLiteral("identityPath"), defaultIdentityPath());
    config.insert(QStringLiteral("fileServerUrl"), QString());
    config.insert(QStringLiteral("fileServerToken"), QString());
    config.insert(QStringLiteral("modelIdentifier"), QStringLiteral("JQOpenClawNode"));
    config.insert(QStringLiteral("permissions"), NodeProfile::permissions());
    return config;
}

QJsonObject NodeApplication::normalizeConfig(const QJsonObject &config)
{
    QJsonObject normalized = defaultConfig();

    const QJsonValue gatewayUrl = config.value(QStringLiteral("gatewayUrl"));
    if ( gatewayUrl.isString() )
    {
        normalized.insert(QStringLiteral("gatewayUrl"), gatewayUrl.toString().trimmed());
    }

    const QJsonValue token = config.value(QStringLiteral("token"));
    if ( token.isString() )
    {
        normalized.insert(QStringLiteral("token"), token.toString());
    }

    const QJsonValue followSystemStartup = config.value(QStringLiteral("followSystemStartup"));
    if ( followSystemStartup.isBool() )
    {
        normalized.insert(
            QStringLiteral("followSystemStartup"),
            followSystemStartup.toBool(false)
        );
    }

    const QJsonValue silentStartup = config.value(QStringLiteral("silentStartup"));
    if ( silentStartup.isBool() )
    {
        normalized.insert(
            QStringLiteral("silentStartup"),
            silentStartup.toBool(false)
        );
    }

    const QJsonValue displayName = config.value(QStringLiteral("displayName"));
    if ( displayName.isString() )
    {
        normalized.insert(QStringLiteral("displayName"), displayName.toString().trimmed());
    }

    const QJsonValue instanceId = config.value(QStringLiteral("instanceId"));
    if ( instanceId.isString() )
    {
        normalized.insert(QStringLiteral("instanceId"), instanceId.toString().trimmed());
    }

    const QJsonValue identityPath = config.value(QStringLiteral("identityPath"));
    if ( identityPath.isString() )
    {
        const QString normalizedIdentityPath = identityPath.toString().trimmed();
        if ( normalizedIdentityPath.isEmpty() )
        {
            normalized.insert(QStringLiteral("identityPath"), defaultIdentityPath());
        }
        else
        {
            normalized.insert(QStringLiteral("identityPath"), normalizedIdentityPath);
        }
    }

    const QJsonValue fileServerUrl = config.value(QStringLiteral("fileServerUrl"));
    if ( fileServerUrl.isString() )
    {
        normalized.insert(QStringLiteral("fileServerUrl"), fileServerUrl.toString().trimmed());
    }

    const QJsonValue fileServerToken = config.value(QStringLiteral("fileServerToken"));
    if ( fileServerToken.isString() )
    {
        normalized.insert(QStringLiteral("fileServerToken"), fileServerToken.toString());
    }

    const QJsonValue modelIdentifier = config.value(QStringLiteral("modelIdentifier"));
    if ( modelIdentifier.isString() )
    {
        normalized.insert(QStringLiteral("modelIdentifier"), modelIdentifier.toString().trimmed());
    }

    normalized.insert(
        QStringLiteral("permissions"),
        NodeProfile::normalizePermissions(
            config.value(QStringLiteral("permissions")).toObject()
        )
    );

    return normalized;
}

QString NodeApplication::defaultIdentityPath()
{
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).trimmed();
    if ( appDataPath.isEmpty() )
    {
        appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).trimmed();
    }
    if ( appDataPath.isEmpty() )
    {
        appDataPath = QDir::homePath() + QStringLiteral("/.jqopenclawnode");
    }

    QDir appDataDirectory(appDataPath);
    return appDataDirectory.filePath(QStringLiteral("identity.json"));
}

bool NodeApplication::reconnectGatewayFromConfig(QString *error)
{
    QString optionsError;
    const NodeOptions options = buildNodeOptions(&optionsError);
    if ( !optionsError.isEmpty() )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = optionsError;
        updateConnectionStatusAction();
        if ( error != nullptr )
        {
            *error = optionsError;
        }
        return false;
    }

    QString authInitError;
    if ( !DeviceAuth::initialize(&authInitError) )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = authInitError.trimmed();
        updateConnectionStatusAction();
        qCritical().noquote() << authInitError;
        if ( error != nullptr )
        {
            *error = authInitError;
        }
        return false;
    }

    DeviceIdentityStore store(options.identityPath);
    QString identityError;
    if ( !store.loadOrCreate(&identity_, &identityError) )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = identityError.trimmed();
        updateConnectionStatusAction();
        qCritical().noquote() << identityError;
        if ( error != nullptr )
        {
            *error = identityError;
        }
        return false;
    }

    gatewayClient_.setOptions(options);

    stopPairingReconnect();
    registered_ = false;
    reconnectingFromConfigSave_ = true;
    reconnectAfterClose_ = true;
    setConnectionState(ConnectionState::Connecting);
    connectionStateDetail_.clear();
    updateConnectionStatusAction();

    gatewayClient_.close();
    QTimer::singleShot(
        200,
        this,
        [this]()
        {
            if ( !reconnectAfterClose_ )
            {
                return;
            }

            reconnectAfterClose_ = false;
            gatewayClient_.open();
        }
    );
    return true;
}

bool NodeApplication::loadConfigFromDisk(QString *error)
{
    QString appConfigPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).trimmed();
    if ( appConfigPath.isEmpty() )
    {
        appConfigPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).trimmed();
    }
    if ( appConfigPath.isEmpty() )
    {
        appConfigPath = QDir::homePath() + QStringLiteral("/.jqopenclawnode");
    }

    QDir appConfigDirectory(appConfigPath);
    if ( !appConfigDirectory.exists() &&
         !appConfigDirectory.mkpath(QStringLiteral(".")) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to create config directory: %1")
                .arg(appConfigDirectory.absolutePath());
        }
        return false;
    }

    configPath_ = appConfigDirectory.filePath(QStringLiteral("config.json"));
    QFileInfo configFileInfo(configPath_);
    if ( !configFileInfo.exists() )
    {
        QJsonObject defaults = defaultConfig();
        defaults.insert(QStringLiteral("displayName"), generateDefaultDisplayName());
        defaults.insert(
            QStringLiteral("instanceId"),
            QUuid::createUuid().toString(QUuid::WithoutBraces)
        );
        if ( !saveConfigToDisk(defaults, error) )
        {
            return false;
        }
        setConfig(defaults);
        return true;
    }

    QFile configFile(configPath_);
    if ( !configFile.open(QIODevice::ReadOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to open config file: %1").arg(configPath_);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument configJson = QJsonDocument::fromJson(
        configFile.readAll(),
        &parseError
    );
    if ( ( parseError.error != QJsonParseError::NoError ) ||
         !configJson.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid config JSON: %1").arg(configPath_);
        }
        return false;
    }

    QJsonObject normalizedConfig = normalizeConfig(configJson.object());
    bool configChanged = false;
    if ( normalizedConfig.value(QStringLiteral("displayName")).toString().trimmed().isEmpty() )
    {
        normalizedConfig.insert(QStringLiteral("displayName"), generateDefaultDisplayName());
        configChanged = true;
    }
    if ( normalizedConfig.value(QStringLiteral("instanceId")).toString().trimmed().isEmpty() )
    {
        normalizedConfig.insert(
            QStringLiteral("instanceId"),
            QUuid::createUuid().toString(QUuid::WithoutBraces)
        );
        configChanged = true;
    }

    if ( configChanged )
    {
        QString saveError;
        if ( !saveConfigToDisk(normalizedConfig, &saveError) )
        {
            // Keep runtime usable even if persisting normalized config fails.
            qWarning().noquote() << QStringLiteral(
                "failed to persist normalized config, continue with in-memory config: %1"
            ).arg(saveError);
        }
    }

    setConfig(normalizedConfig);
    return true;
}

bool NodeApplication::saveConfigToDisk(
    const QJsonObject &config,
    QString *error
) const
{
    if ( configPath_.trimmed().isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("config path is empty");
        }
        return false;
    }

    const QFileInfo configFileInfo(configPath_);
    QDir configDirectory(configFileInfo.absolutePath());
    if ( !configDirectory.exists() &&
         !configDirectory.mkpath(QStringLiteral(".")) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to create config directory: %1")
                .arg(configDirectory.absolutePath());
        }
        return false;
    }

    QSaveFile configFile(configPath_);
    if ( !configFile.open(QIODevice::WriteOnly | QIODevice::Truncate) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to open config for write: %1").arg(configPath_);
        }
        return false;
    }

    const QByteArray jsonBytes = QJsonDocument(config).toJson(QJsonDocument::Indented);
    if ( configFile.write(jsonBytes) != jsonBytes.size() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to write config data: %1").arg(configPath_);
        }
        configFile.cancelWriting();
        return false;
    }

    if ( !configFile.commit() )
    {
        if ( error != nullptr )
        {
            const QString commitError = configFile.errorString().trimmed();
            if ( commitError.isEmpty() )
            {
                *error = QStringLiteral("failed to commit config file: %1").arg(configPath_);
            }
            else
            {
                *error = QStringLiteral(
                    "failed to commit config file: %1 (%2)"
                ).arg(configPath_, commitError);
            }
        }
        return false;
    }

    return true;
}

NodeOptions NodeApplication::buildNodeOptions(QString *error) const
{
    const QJsonObject normalizedConfig = normalizeConfig(config_);

    NodeOptions options;
    options.gatewayUrl = normalizedConfig.value(QStringLiteral("gatewayUrl")).toString().trimmed();
    options.token = normalizedConfig.value(QStringLiteral("token")).toString();
    options.tls = false;
    options.displayName = normalizedConfig.value(QStringLiteral("displayName")).toString().trimmed();
    options.instanceId = normalizedConfig.value(QStringLiteral("instanceId")).toString().trimmed();
    options.configPath = configPath_;
    options.identityPath = normalizedConfig.value(QStringLiteral("identityPath")).toString().trimmed();
    options.fileServerUrl = normalizedConfig.value(QStringLiteral("fileServerUrl")).toString().trimmed();
    options.fileServerToken = normalizedConfig.value(QStringLiteral("fileServerToken")).toString();
    options.deviceFamily = QStringLiteral("windows-pc");
    options.modelIdentifier = normalizedConfig.value(QStringLiteral("modelIdentifier")).toString().trimmed();
    options.commandPermissions = NodeProfile::normalizePermissions(
        normalizedConfig.value(QStringLiteral("permissions")).toObject()
    );
    options.exitAfterRegister = false;

    if ( options.displayName.isEmpty() )
    {
        options.displayName = generateDefaultDisplayName();
    }
    if ( options.identityPath.isEmpty() )
    {
        options.identityPath = defaultIdentityPath();
    }
    if ( options.modelIdentifier.isEmpty() )
    {
        options.modelIdentifier = QStringLiteral("JQOpenClawNode");
    }

    if ( options.gatewayUrl.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("gateway url is empty");
        }
        return options;
    }

    const QUrl gatewayUrl(options.gatewayUrl);
    const QString gatewayScheme = gatewayUrl.scheme().trimmed().toLower();
    if ( !gatewayUrl.isValid() ||
         gatewayScheme.isEmpty() ||
         gatewayUrl.host().trimmed().isEmpty() ||
         ( gatewayScheme != QStringLiteral("ws") &&
           gatewayScheme != QStringLiteral("wss") ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid gateway url");
        }
        return options;
    }
    options.tls = ( gatewayScheme == QStringLiteral("wss") );
    options.gatewayUrl = gatewayUrl.toString(QUrl::FullyEncoded);

    if ( options.token.trimmed().isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("gateway token is empty");
        }
        return options;
    }

    if ( !options.fileServerUrl.isEmpty() )
    {
        const QUrl fileServerUrl(options.fileServerUrl);
        if ( !fileServerUrl.isValid() ||
             fileServerUrl.scheme().trimmed().isEmpty() ||
             fileServerUrl.host().trimmed().isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("invalid file server url");
            }
            return options;
        }
    }

    return options;
}

QString NodeApplication::configString(
    const QString &key,
    const QString &defaultValue
) const
{
    const QJsonValue value = config_.value(key);
    return value.isString() ? value.toString() : defaultValue;
}

void NodeApplication::initializeSystemTray()
{
    if ( trayIcon_ != nullptr )
    {
        return;
    }

    if ( !QSystemTrayIcon::isSystemTrayAvailable() )
    {
        qWarning().noquote() << QStringLiteral("system tray is not available");
        return;
    }

    trayMenu_ = new QMenu();
    mainWindowAction_ = trayMenu_->addAction(QStringLiteral("操作面板"));
    exitAction_ = trayMenu_->addAction(QStringLiteral("退出程序"));

    connect(
        mainWindowAction_,
        &QAction::triggered,
        this,
        &NodeApplication::onMainWindowActionTriggered
    );
    connect(
        exitAction_,
        &QAction::triggered,
        this,
        &NodeApplication::onExitActionTriggered
    );

    trayIcon_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/icon/icon.ico")), this);
    trayIcon_->setToolTip(QStringLiteral("JQOpenClawNode"));
    connect(
        trayIcon_,
        &QSystemTrayIcon::activated,
        this,
        &NodeApplication::onTrayIconActivated
    );

    updateConnectionStatusAction();
    trayIcon_->show();
}

void NodeApplication::hideTrayIconIfNeeded()
{
    if ( trayIcon_ == nullptr )
    {
        return;
    }

    if ( trayIcon_->isVisible() )
    {
        trayIcon_->hide();
    }
}

void NodeApplication::requestApplicationExit(int code)
{
    hideTrayIconIfNeeded();
    QCoreApplication::exit(code);
}

void NodeApplication::showMainWindow()
{
    if ( mainWindowObject_.isNull() )
    {
        qWarning().noquote() << QStringLiteral("[tray] main window object is null");
        return;
    }

    auto window = qobject_cast< QWindow * >( mainWindowObject_.data() );
    if ( window == nullptr )
    {
        qWarning().noquote() << QStringLiteral("[tray] main window object is not a QWindow");
        return;
    }

    if ( !window->isVisible() )
    {
        window->show();
    }

    if ( window->windowState() == Qt::WindowMinimized )
    {
        window->setWindowState(Qt::WindowNoState);
    }

    window->setOpacity(1.0);
    window->raise();
    window->requestActivate();
}

void NodeApplication::updateConnectionStatusAction()
{
    const QString statusText = connectionStateDisplayText();
    if ( statusText != connectionStateText_ )
    {
        connectionStateText_ = statusText;
        emit connectionStateTextChanged(connectionStateText_);
    }

    if ( trayIcon_ != nullptr )
    {
        const QString detailText = connectionStateDetail_.trimmed();
        QString trayToolTip = QStringLiteral("JQOpenClawNode\n连接状态：%1")
            .arg(statusText);
        const bool statusAlreadyContainsDetail = (
            ( connectionState_ == ConnectionState::Error ) &&
            !detailText.isEmpty() &&
            statusText.contains(detailText)
        );
        if ( ( connectionState_ != ConnectionState::Pairing ) &&
             !detailText.isEmpty() &&
             !statusAlreadyContainsDetail )
        {
            trayToolTip.append(
                QStringLiteral("\n详情：%1").arg(detailText.left(120))
            );
        }
        trayIcon_->setToolTip(trayToolTip);
    }
}

QString NodeApplication::connectionStateDisplayText() const
{
    switch ( connectionState_ )
    {
    case ConnectionState::Disconnected:
        return QStringLiteral("未连接");
    case ConnectionState::Connecting:
        return QStringLiteral("连接中");
    case ConnectionState::Pairing:
        return QStringLiteral("配对中");
    case ConnectionState::Connected:
        return QStringLiteral("已连接");
    case ConnectionState::Error:
        if ( connectionStateDetail_.isEmpty() )
        {
            return QStringLiteral("异常");
        }
        return QStringLiteral("异常(%1)").arg(connectionStateDetail_);
    }

    return QStringLiteral("未知");
}

void NodeApplication::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if ( ( reason == QSystemTrayIcon::Trigger ) ||
         ( reason == QSystemTrayIcon::DoubleClick ) )
    {
        showMainWindow();
        return;
    }

    if ( reason != QSystemTrayIcon::Context )
    {
        return;
    }

    if ( trayMenu_ == nullptr )
    {
        return;
    }

    if ( trayMenu_->isVisible() )
    {
        return;
    }

    trayMenu_->popup(QCursor::pos());
}

void NodeApplication::onMainWindowActionTriggered()
{
    showMainWindow();
}

void NodeApplication::onExitActionTriggered()
{
    const QMessageBox::StandardButton result = QMessageBox::question(
        nullptr,
        QStringLiteral("退出确认"),
        QStringLiteral("确定要退出 JQOpenClawNode 吗？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if ( result != QMessageBox::Yes )
    {
        return;
    }

    requestApplicationExit(0);
}

void NodeApplication::start()
{
    initializeSystemTray();
    stopPairingReconnect();
    reconnectAfterClose_ = false;
    reconnectingFromConfigSave_ = false;
    setConnectionState(ConnectionState::Connecting);
    connectionStateDetail_.clear();
    updateConnectionStatusAction();

    const QJsonObject normalizedConfig = normalizeConfig(config_);
    if ( normalizedConfig != config_ )
    {
        setConfig(normalizedConfig);
    }

    QString optionsError;
    const NodeOptions options = buildNodeOptions(&optionsError);
    if ( !optionsError.isEmpty() )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = optionsError;
        updateConnectionStatusAction();
        qWarning().noquote() << optionsError;
        return;
    }

    QString initError;
    if ( !DeviceAuth::initialize(&initError) )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = initError.trimmed();
        updateConnectionStatusAction();
        qCritical().noquote() << initError;
        return;
    }

    DeviceIdentityStore store(options.identityPath);
    QString identityError;
    if ( !store.loadOrCreate(&identity_, &identityError) )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = identityError.trimmed();
        updateConnectionStatusAction();
        qCritical().noquote() << identityError;
        return;
    }

    QString selfTestError;
    if ( !runCryptoSelfTest(&selfTestError) )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = selfTestError.trimmed();
        updateConnectionStatusAction();
        qCritical().noquote() << selfTestError;
        return;
    }

    qInfo().noquote() << QStringLiteral("device identity: %1").arg(identity_.deviceId);
    qInfo().noquote() << QStringLiteral("identity file: %1").arg(store.identityPath());
    const QString configuredInstanceId = options.instanceId.trimmed();
    if ( !configuredInstanceId.isEmpty() )
    {
        qInfo().noquote() << QStringLiteral("node instance id: %1").arg(configuredInstanceId);
    }

    gatewayClient_.setOptions(options);
    gatewayClient_.open();
}

void NodeApplication::onChallengeReceived(const QString &nonce)
{
    sendConnectRequest(nonce);
}

void NodeApplication::onConnectAccepted(const QJsonObject &payload)
{
    stopPairingReconnect();
    reconnectAfterClose_ = false;
    reconnectingFromConfigSave_ = false;
    registered_ = true;
    setConnectionState(ConnectionState::Connected);
    connectionStateDetail_.clear();
    updateConnectionStatusAction();

    const QJsonObject authObject = payload.value("auth").toObject();
    const QString deviceToken = authObject.value("deviceToken").toString().trimmed();
    if ( deviceToken.isEmpty() )
    {
        qInfo().noquote() << QStringLiteral("node registered successfully");
    }
    else
    {
        qInfo().noquote() << QStringLiteral("node registered successfully, device token issued");
    }

}

void NodeApplication::onConnectRejected(const QJsonObject &error)
{
    const QString message = parseErrorMessage(error);
    if ( isPairingRequiredConnectError(error) )
    {
        setConnectionState(ConnectionState::Pairing);
        connectionStateDetail_ = message;
        updateConnectionStatusAction();
        qWarning().noquote() << QStringLiteral("gateway connect rejected, waiting for pairing: %1").arg(message);
        startPairingReconnect();
        return;
    }

    setConnectionState(ConnectionState::Error);
    connectionStateDetail_ = message;
    updateConnectionStatusAction();
    qWarning().noquote() << QStringLiteral("gateway connect rejected: %1").arg(message);
    startPairingReconnect();
}

void NodeApplication::appendInvokeHistoryEntry(
    const QString &invokeTime,
    const QString &capability
)
{
    QVariantList nextHistory = invokeHistory_;
    QVariantMap historyEntry;
    historyEntry.insert(
        QStringLiteral("time"),
        invokeTime.trimmed().isEmpty()
            ? QStringLiteral("未知时间")
            : invokeTime.trimmed()
    );
    historyEntry.insert(
        QStringLiteral("capability"),
        capability.trimmed().isEmpty()
            ? QStringLiteral("未知能力")
            : capability.trimmed()
    );
    nextHistory.prepend(historyEntry);
    while ( nextHistory.size() > kInvokeHistoryMaxEntries )
    {
        nextHistory.removeLast();
    }
    setInvokeHistory(nextHistory);
}

void NodeApplication::onInvokeRequestReceived(const QJsonObject &payload)
{
    const QString invokeId = extractString(payload, QStringLiteral("id"));
    const QString nodeId = extractString(payload, QStringLiteral("nodeId"));
    const QString command = extractString(payload, QStringLiteral("command"));
    const QString invokeTime = QDateTime::currentDateTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    appendInvokeHistoryEntry(invokeTime, command);
    const QString idempotencyKey = extractString(payload, QStringLiteral("idempotencyKey"));
    const QJsonValue paramsJsonValue = payload.value(QStringLiteral("paramsJSON"));
    const QString paramsJson = paramsJsonValue.isString()
        ? paramsJsonValue.toString().trimmed()
        : QString();

    qInfo().noquote() << QStringLiteral(
        "[node.invoke] request received id=%1 command=%2 paramsJSON=%3"
    ).arg(invokeId, command, paramsJson);

    if ( invokeId.isEmpty() || nodeId.isEmpty() || command.isEmpty() )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] request ignored: missing id/nodeId/command"
        );
        return;
    }

    const QString localDeviceId = identity_.deviceId.trimmed();
    if ( localDeviceId.isEmpty() )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] invalid state id=%1 command=%2 error=local device identity is empty"
        ).arg(invokeId, command);
        sendInvokeError(
            invokeId,
            nodeId,
            QStringLiteral("INTERNAL_ERROR"),
            QStringLiteral("local device identity is empty")
        );
        return;
    }
    if ( nodeId != localDeviceId )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] nodeId mismatch id=%1 command=%2"
        ).arg(invokeId, command);
        sendInvokeError(
            invokeId,
            nodeId,
            QStringLiteral("INVALID_PARAMS"),
            QStringLiteral("nodeId does not match local device identity")
        );
        return;
    }

    if ( idempotencyKey.isEmpty() )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] invalid request id=%1 command=%2 error=missing idempotencyKey"
        ).arg(invokeId, command);
        sendInvokeError(
            invokeId,
            nodeId,
            QStringLiteral("INVALID_PARAMS"),
            QStringLiteral("idempotencyKey is required")
        );
        return;
    }

    QJsonValue params = QJsonObject();
    QString parseError;
    if ( !parseInvokeParamsJson(paramsJsonValue, &params, &parseError) )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] invalid params id=%1 command=%2 error=%3"
        ).arg(invokeId, command, parseError);
        sendInvokeError(
            invokeId,
            nodeId,
            QStringLiteral("INVALID_PARAMS"),
            parseError
        );
        return;
    }

    int invokeTimeoutMs = -1;
    if ( !parseInvokeTimeoutMs(payload, &invokeTimeoutMs, &parseError) )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] invalid timeout id=%1 command=%2 error=%3"
        ).arg(invokeId, command, parseError);
        sendInvokeError(
            invokeId,
            nodeId,
            QStringLiteral("INVALID_PARAMS"),
            parseError
        );
        return;
    }

    const QString invokeCacheKey = buildInvokeIdempotencyCacheKey(
        nodeId,
        command,
        idempotencyKey
    );
    const QString requestFingerprint = buildInvokeRequestFingerprint(
        command,
        params,
        invokeTimeoutMs
    );

    auto pruneInvokeIdempotencyCache = [this](qint64 nowMs)
    {
        for ( int index = invokeIdempotencyCacheOrder_.size() - 1; index >= 0; --index )
        {
            const QString cacheKey = invokeIdempotencyCacheOrder_.at(index);
            QHash<QString, InvokeIdempotencyEntry>::iterator cacheIter =
                invokeIdempotencyCache_.find(cacheKey);
            if ( cacheIter == invokeIdempotencyCache_.end() )
            {
                invokeIdempotencyCacheOrder_.removeAt(index);
                continue;
            }

            if ( !cacheIter->completed )
            {
                continue;
            }

            if ( ( nowMs - cacheIter->updatedAtMs ) <= kInvokeIdempotencyCacheTtlMs )
            {
                continue;
            }

            invokeIdempotencyCache_.erase(cacheIter);
            invokeIdempotencyCacheOrder_.removeAt(index);
        }

        while ( invokeIdempotencyCache_.size() > kInvokeIdempotencyCacheMaxEntries )
        {
            int removeIndex = -1;
            for ( int index = 0; index < invokeIdempotencyCacheOrder_.size(); ++index )
            {
                const QString cacheKey = invokeIdempotencyCacheOrder_.at(index);
                QHash<QString, InvokeIdempotencyEntry>::iterator cacheIter =
                    invokeIdempotencyCache_.find(cacheKey);
                if ( cacheIter == invokeIdempotencyCache_.end() )
                {
                    removeIndex = index;
                    break;
                }

                if ( cacheIter->completed )
                {
                    invokeIdempotencyCache_.erase(cacheIter);
                    removeIndex = index;
                    break;
                }
            }

            if ( removeIndex < 0 )
            {
                break;
            }

            invokeIdempotencyCacheOrder_.removeAt(removeIndex);
        }
    };

    auto sendInvokeResultToTarget = [this](
        const QString &targetInvokeId,
        const QString &targetNodeId,
        bool ok,
        const QJsonValue &resultPayload,
        const QString &resultErrorCode,
        const QString &resultErrorMessage
    )
    {
        if ( ok )
        {
            sendInvokeSuccess(targetInvokeId, targetNodeId, resultPayload);
            return;
        }

        sendInvokeError(
            targetInvokeId,
            targetNodeId,
            resultErrorCode,
            resultErrorMessage
        );
    };

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    pruneInvokeIdempotencyCache(nowMs);

    QHash<QString, InvokeIdempotencyEntry>::iterator existingInvokeCacheIter =
        invokeIdempotencyCache_.find(invokeCacheKey);
    if ( existingInvokeCacheIter != invokeIdempotencyCache_.end() )
    {
        if ( existingInvokeCacheIter->requestFingerprint != requestFingerprint )
        {
            const QString message = QStringLiteral(
                "idempotencyKey cannot be reused with different request parameters"
            );
            qWarning().noquote() << QStringLiteral(
                "[node.invoke] invalid idempotency key id=%1 command=%2 error=%3"
            ).arg(invokeId, command, message);
            sendInvokeError(
                invokeId,
                nodeId,
                QStringLiteral("INVALID_PARAMS"),
                message
            );
            return;
        }

        existingInvokeCacheIter->updatedAtMs = nowMs;
        if ( existingInvokeCacheIter->completed )
        {
            sendInvokeResultToTarget(
                invokeId,
                nodeId,
                existingInvokeCacheIter->ok,
                existingInvokeCacheIter->payload,
                existingInvokeCacheIter->errorCode,
                existingInvokeCacheIter->errorMessage
            );
            qInfo().noquote() << QStringLiteral(
                "[node.invoke] replayed idempotent result id=%1 command=%2 key=%3"
            ).arg(invokeId, command, idempotencyKey);
            pruneInvokeIdempotencyCache(nowMs);
            return;
        }

        bool alreadyQueued = false;
        for ( const InvokeReplayTarget &target : existingInvokeCacheIter->waitingTargets )
        {
            if ( target.invokeId == invokeId && target.nodeId == nodeId )
            {
                alreadyQueued = true;
                break;
            }
        }
        if ( !alreadyQueued )
        {
            InvokeReplayTarget target;
            target.invokeId = invokeId;
            target.nodeId = nodeId;
            existingInvokeCacheIter->waitingTargets.append(target);
        }

        qInfo().noquote() << QStringLiteral(
            "[node.invoke] request joined in-flight idempotency id=%1 command=%2 key=%3"
        ).arg(invokeId, command, idempotencyKey);
        return;
    }

    InvokeIdempotencyEntry newInvokeEntry;
    newInvokeEntry.requestFingerprint = requestFingerprint;
    newInvokeEntry.updatedAtMs = nowMs;
    invokeIdempotencyCache_.insert(invokeCacheKey, newInvokeEntry);
    invokeIdempotencyCacheOrder_.append(invokeCacheKey);
    pruneInvokeIdempotencyCache(nowMs);

    auto finalizeInvokeResult = [this,
                                 &invokeCacheKey,
                                 &invokeId,
                                 &nodeId,
                                 &pruneInvokeIdempotencyCache,
                                 &sendInvokeResultToTarget](
                                    bool ok,
                                    const QJsonValue &resultPayload,
                                    const QString &resultErrorCode,
                                    const QString &resultErrorMessage
                                )
    {
        const qint64 finishMs = QDateTime::currentMSecsSinceEpoch();
        QList<InvokeReplayTarget> waitingTargets;

        QHash<QString, InvokeIdempotencyEntry>::iterator cacheIter =
            invokeIdempotencyCache_.find(invokeCacheKey);
        if ( cacheIter != invokeIdempotencyCache_.end() )
        {
            cacheIter->completed = true;
            cacheIter->ok = ok;
            cacheIter->payload = resultPayload;
            cacheIter->errorCode = resultErrorCode;
            cacheIter->errorMessage = resultErrorMessage;
            cacheIter->updatedAtMs = finishMs;
            waitingTargets = cacheIter->waitingTargets;
            cacheIter->waitingTargets.clear();
        }

        sendInvokeResultToTarget(
            invokeId,
            nodeId,
            ok,
            resultPayload,
            resultErrorCode,
            resultErrorMessage
        );

        for ( const InvokeReplayTarget &target : waitingTargets )
        {
            if ( target.invokeId == invokeId && target.nodeId == nodeId )
            {
                continue;
            }

            sendInvokeResultToTarget(
                target.invokeId,
                target.nodeId,
                ok,
                resultPayload,
                resultErrorCode,
                resultErrorMessage
            );
        }

        pruneInvokeIdempotencyCache(finishMs);
    };

    if ( invokeTimeoutMs == 0 )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] timeout immediately id=%1 command=%2"
        ).arg(invokeId, command);
        finalizeInvokeResult(
            false,
            QJsonValue(),
            QStringLiteral("TIMEOUT"),
            QStringLiteral("node invoke timed out")
        );
        return;
    }

    QJsonValue responsePayload;
    QString errorCode;
    QString errorMessage;
    if ( !executeInvokeCommand(
            command,
            params,
            invokeTimeoutMs,
            &responsePayload,
            &errorCode,
            &errorMessage
        ) )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] command failed id=%1 command=%2 code=%3 message=%4"
        ).arg(invokeId, command, errorCode, errorMessage);
        finalizeInvokeResult(false, QJsonValue(), errorCode, errorMessage);
        return;
    }

    finalizeInvokeResult(true, responsePayload, QString(), QString());
    qInfo().noquote() << QStringLiteral(
        "[node.invoke] command done id=%1 command=%2"
    ).arg(invokeId, command);

    if ( command == QStringLiteral("node.selfUpdate") &&
         responsePayload.isObject() )
    {
        const QJsonObject selfUpdatePayload = responsePayload.toObject();
        if ( selfUpdatePayload.value(QStringLiteral("willExit")).toBool(false) )
        {
            qInfo().noquote() << QStringLiteral(
                "[node.selfUpdate] invoke response sent, exit in %1ms"
            ).arg(QString::number(kSelfUpdateExitDelayMs));
            QTimer::singleShot(
                kSelfUpdateExitDelayMs,
                this,
                [this]()
                {
                    requestApplicationExit(0);
                }
            );
        }
    }
}

void NodeApplication::onTransportError(const QString &message)
{
    const bool pairingInProgress = ( connectionState_ == ConnectionState::Pairing );
    if ( !pairingInProgress )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = message.trimmed();
        updateConnectionStatusAction();
    }
    else
    {
        connectionStateDetail_ = QStringLiteral("等待配对完成");
        updateConnectionStatusAction();
    }

    qCritical().noquote() << message;
    startPairingReconnect();
    if ( !registered_ )
    {
        if ( reconnectingFromConfigSave_ )
        {
            qWarning().noquote() << QStringLiteral("transport error after config save");
            return;
        }

        if ( pairingInProgress )
        {
            qInfo().noquote() << QStringLiteral("transport error while pairing");
            return;
        }
        qWarning().noquote() << QStringLiteral("transport error before registration");
        return;
    }

    qWarning().noquote() << QStringLiteral("transport error after registration, waiting for reconnect");
}

void NodeApplication::onGatewayClosed()
{
    if ( reconnectAfterClose_ )
    {
        reconnectAfterClose_ = false;
        setConnectionState(ConnectionState::Connecting);
        connectionStateDetail_.clear();
        updateConnectionStatusAction();
        gatewayClient_.open();
        return;
    }

    if ( !registered_ &&
         pairingReconnectTimer_.isActive() &&
         ( connectionState_ == ConnectionState::Error ||
           connectionState_ == ConnectionState::Connecting ) )
    {
        qInfo().noquote() << QStringLiteral("gateway closed after transport error, waiting for retry");
        return;
    }

    if ( connectionState_ == ConnectionState::Pairing )
    {
        connectionStateDetail_ = QStringLiteral("等待配对完成");
        updateConnectionStatusAction();
        qInfo().noquote() << QStringLiteral("gateway closed while pairing");
        return;
    }

    if ( !registered_ )
    {
        if ( connectionState_ != ConnectionState::Error )
        {
            setConnectionState(ConnectionState::Error);
            if ( connectionStateDetail_.trimmed().isEmpty() )
            {
                connectionStateDetail_ = QStringLiteral("网关连接已关闭");
            }
            updateConnectionStatusAction();
        }

        startPairingReconnect();

        if ( reconnectingFromConfigSave_ )
        {
            qWarning().noquote() << QStringLiteral("gateway closed after config save");
            return;
        }

        qWarning().noquote() << QStringLiteral("gateway closed before registration");
        return;
    }

    setConnectionState(ConnectionState::Disconnected);
    connectionStateDetail_ = QStringLiteral("网关连接已关闭");
    updateConnectionStatusAction();

    startPairingReconnect();
    qWarning().noquote() << QStringLiteral("gateway closed after registration, waiting for reconnect");
}

void NodeApplication::onPairingReconnectTimeout()
{
    if ( connectionState_ != ConnectionState::Pairing &&
         connectionState_ != ConnectionState::Disconnected &&
         connectionState_ != ConnectionState::Error &&
         connectionState_ != ConnectionState::Connecting )
    {
        stopPairingReconnect();
        return;
    }

    if ( connectionState_ == ConnectionState::Error ||
         connectionState_ == ConnectionState::Disconnected )
    {
        setConnectionState(ConnectionState::Connecting);
        connectionStateDetail_.clear();
        updateConnectionStatusAction();
    }

    qInfo().noquote() << QStringLiteral(
        "pairing reconnect tick, retry gateway connection in %1 ms"
    ).arg(kPairingReconnectIntervalMs);
    gatewayClient_.close();
    QTimer::singleShot(
        200,
        this,
        [this]()
        {
            if ( connectionState_ == ConnectionState::Connecting ||
                 connectionState_ == ConnectionState::Disconnected ||
                 connectionState_ == ConnectionState::Pairing ||
                 connectionState_ == ConnectionState::Error )
            {
                gatewayClient_.open();
            }
        }
    );
}

void NodeApplication::startPairingReconnect()
{
    if ( pairingReconnectTimer_.isActive() )
    {
        return;
    }
    pairingReconnectTimer_.start();
    qInfo().noquote() << QStringLiteral(
        "pairing reconnect timer started, interval=%1 ms"
    ).arg(kPairingReconnectIntervalMs);
}

void NodeApplication::stopPairingReconnect()
{
    if ( !pairingReconnectTimer_.isActive() )
    {
        return;
    }
    pairingReconnectTimer_.stop();
    qInfo().noquote() << QStringLiteral("pairing reconnect timer stopped");
}

bool NodeApplication::runCryptoSelfTest(QString *error) const
{
    const QByteArray key = SecretBoxCrypto::generateKey();
    const QByteArray plainText("jqopenclaw-self-test");
    QByteArray nonce;
    QByteArray cipherText;
    QByteArray recovered;

    QString cryptoError;
    if ( !SecretBoxCrypto::encrypt(key, plainText, &nonce, &cipherText, &cryptoError) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("crypto self-test encrypt failed: %1").arg(cryptoError);
        }
        return false;
    }

    if ( !SecretBoxCrypto::decrypt(key, nonce, cipherText, &recovered, &cryptoError) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("crypto self-test decrypt failed: %1").arg(cryptoError);
        }
        return false;
    }

    if ( recovered != plainText )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("crypto self-test round-trip mismatch");
        }
        return false;
    }

    return true;
}

bool NodeApplication::parseInvokeParamsJson(
    const QJsonValue &paramsJsonValue,
    QJsonValue *params,
    QString *error
) const
{
    if ( params == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invoke params output pointer is null");
        }
        return false;
    }

    if ( paramsJsonValue.isUndefined() || paramsJsonValue.isNull() )
    {
        *params = QJsonObject();
        return true;
    }

    if ( !paramsJsonValue.isString() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("paramsJSON must be string");
        }
        return false;
    }

    const QString normalized = paramsJsonValue.toString().trimmed();
    if ( normalized.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("paramsJSON must be object");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(
        normalized.toUtf8(),
        &parseError
    );
    if ( parseError.error != QJsonParseError::NoError )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to parse paramsJSON: %1")
                .arg(parseError.errorString());
        }
        return false;
    }

    if ( json.isObject() )
    {
        *params = json.object();
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("paramsJSON must be object");
    }
    return false;
}

bool NodeApplication::executeInvokeCommand(
    const QString &command,
    const QJsonValue &params,
    int invokeTimeoutMs,
    QJsonValue *payload,
    QString *errorCode,
    QString *errorMessage
) const
{
    if ( payload == nullptr )
    {
        if ( errorCode != nullptr )
        {
            *errorCode = QStringLiteral("INTERNAL_ERROR");
        }
        if ( errorMessage != nullptr )
        {
            *errorMessage = QStringLiteral("invoke payload output pointer is null");
        }
        return false;
    }

    const QJsonObject permissions = NodeProfile::normalizePermissions(
        normalizeConfig(config_).value(QStringLiteral("permissions")).toObject()
    );
    if ( NodeProfile::isKnownCommand(command) &&
         !NodeProfile::isCommandEnabled(command, permissions) )
    {
        if ( errorCode != nullptr )
        {
            *errorCode = QStringLiteral("PERMISSION_DENIED");
        }
        if ( errorMessage != nullptr )
        {
            *errorMessage = QStringLiteral("command disabled by node permission: %1").arg(command);
        }
        return false;
    }

    if ( command == QStringLiteral("node.selfUpdate") )
    {
        QJsonObject selfUpdateResult;
        QString selfUpdateError;
        bool invalidParams = false;
        bool md5Mismatch = false;
        if ( !NodeSelfUpdate::execute(
                params,
                &selfUpdateResult,
                &selfUpdateError,
                &invalidParams,
                &md5Mismatch
            ) )
        {
            if ( errorCode != nullptr )
            {
                if ( invalidParams )
                {
                    *errorCode = QStringLiteral("INVALID_PARAMS");
                }
                else if ( md5Mismatch )
                {
                    *errorCode = QStringLiteral("NODE_SELF_UPDATE_MD5_MISMATCH");
                }
                else
                {
                    *errorCode = QStringLiteral("NODE_SELF_UPDATE_FAILED");
                }
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = selfUpdateError.isEmpty()
                    ? QStringLiteral("failed to execute node self update")
                    : selfUpdateError;
            }
            return false;
        }

        *payload = selfUpdateResult;
        return true;
    }

    if ( command == QStringLiteral("system.info") )
    {
        QJsonObject info;
        QString collectError;
        if ( !SystemInfo::collect(&info, &collectError) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = QStringLiteral("SYSTEM_INFO_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = collectError.isEmpty()
                    ? QStringLiteral("failed to collect system info")
                    : collectError;
            }
            return false;
        }
        *payload = info;
        return true;
    }

    if ( command == QStringLiteral("system.screenshot") )
    {
        QList<SystemScreenshot::CaptureResult> captures;
        QString captureError;
        const QString fileServerUrl = configString(QStringLiteral("fileServerUrl")).trimmed();
        const QString fileServerToken = configString(QStringLiteral("fileServerToken"));
        if ( !SystemScreenshot::captureAllToJpg(&captures, &captureError) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = QStringLiteral("SCREENSHOT_CAPTURE_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = captureError.isEmpty()
                    ? QStringLiteral("failed to capture screenshot")
                    : captureError;
            }
            return false;
        }

        QJsonArray resultArray;
        for ( int index = 0; index < captures.size(); ++index )
        {
            const SystemScreenshot::CaptureResult &captureResult = captures.at(index);
            if ( captureResult.jpgBytes.isEmpty() )
            {
                qWarning().noquote() << QStringLiteral(
                    "[capability.system.screenshot] upload screen skipped index=%1 reason=empty image bytes"
                ).arg(captureResult.screenIndex);
                continue;
            }

            QString fileUrl;
            QString uploadError;
            if ( !uploadScreenshotFile(
                    captureResult.jpgBytes,
                    fileServerUrl,
                    fileServerToken,
                    &fileUrl,
                    &uploadError
                ) )
            {
                qWarning().noquote() << QStringLiteral(
                    "[capability.system.screenshot] upload screen skipped index=%1 reason=%2"
                ).arg( QString::number( captureResult.screenIndex ), uploadError );
                continue;
            }
            qInfo().noquote() << QStringLiteral(
                "[capability.system.screenshot] upload done index=%1 url=%2"
            ).arg( QString::number( captureResult.screenIndex ), fileUrl );

            QJsonObject result;
            result.insert(QStringLiteral("format"), QStringLiteral("jpg"));
            result.insert(QStringLiteral("mimeType"), QStringLiteral("image/jpeg"));
            result.insert(QStringLiteral("url"), fileUrl);
            result.insert(QStringLiteral("width"), captureResult.scaledSize.width());
            result.insert(QStringLiteral("height"), captureResult.scaledSize.height());
            result.insert(QStringLiteral("screenIndex"), captureResult.screenIndex);
            if ( !captureResult.screenName.trimmed().isEmpty() )
            {
                result.insert(QStringLiteral("screenName"), captureResult.screenName);
            }
            resultArray.append(result);
        }

        if ( resultArray.isEmpty() )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = QStringLiteral("SCREENSHOT_UPLOAD_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = QStringLiteral("failed to upload screenshots for all screens");
            }
            return false;
        }

        *payload = resultArray;
        return true;
    }

    if ( command == QStringLiteral("system.input") )
    {
        QJsonObject inputResult;
        QString inputError;
        bool invalidParams = false;
        if ( !SystemInput::execute(
                params,
                &inputResult,
                &inputError,
                &invalidParams
            ) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("SYSTEM_INPUT_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = inputError.isEmpty()
                    ? QStringLiteral("failed to run system input")
                    : inputError;
            }
            return false;
        }

        *payload = inputResult;
        return true;
    }

    if ( command == QStringLiteral("system.notify") )
    {
        QJsonObject notifyResult;
        QString notifyError;
        bool invalidParams = false;
        if ( !SystemNotify::execute(
                params,
                &notifyResult,
                &notifyError,
                &invalidParams
            ) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("SYSTEM_NOTIFY_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = notifyError.isEmpty()
                    ? QStringLiteral("failed to run system notify")
                    : notifyError;
            }
            return false;
        }

        *payload = notifyResult;
        return true;
    }

    if ( command == QStringLiteral("system.clipboard") )
    {
        QJsonObject clipboardResult;
        QString clipboardError;
        bool invalidParams = false;
        if ( !SystemClipboard::execute(
                params,
                &clipboardResult,
                &clipboardError,
                &invalidParams
            ) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("SYSTEM_CLIPBOARD_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = clipboardError.isEmpty()
                    ? QStringLiteral("failed to run system clipboard")
                    : clipboardError;
            }
            return false;
        }

        *payload = clipboardResult;
        return true;
    }

    if ( command == QStringLiteral("process.exec") )
    {
        QJsonObject executeResult;
        QString executeError;
        bool invalidParams = false;
        if ( !ProcessExec::execute(
                params,
                invokeTimeoutMs,
                &executeResult,
                &executeError,
                &invalidParams
            ) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("PROCESS_EXEC_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = executeError.isEmpty()
                    ? QStringLiteral("failed to execute process")
                    : executeError;
            }
            return false;
        }

        *payload = executeResult;
        return true;
    }

    if ( command == QStringLiteral("process.which") )
    {
        QJsonObject whichResult;
        QString whichError;
        bool invalidParams = false;
        if ( !ProcessWhich::execute(
                params,
                &whichResult,
                &whichError,
                &invalidParams
            ) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("PROCESS_WHICH_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = whichError.isEmpty()
                    ? QStringLiteral("failed to locate executable")
                    : whichError;
            }
            return false;
        }

        *payload = whichResult;
        return true;
    }

    if ( command == QStringLiteral("process.manage") )
    {
        QJsonObject manageResult;
        QString manageError;
        bool invalidParams = false;
        if ( !ProcessManage::execute(
                params,
                &manageResult,
                &manageError,
                &invalidParams
            ) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("PROCESS_MANAGE_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = manageError.isEmpty()
                    ? QStringLiteral("failed to manage process")
                    : manageError;
            }
            return false;
        }

        *payload = manageResult;
        return true;
    }

    if ( command == QStringLiteral("file.read") )
    {
        QJsonObject readResult;
        QString readError;
        bool invalidParams = false;
        if ( !FileReadAccess::read(
                params,
                invokeTimeoutMs,
                &readResult,
                &readError,
                &invalidParams
            ) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("FILE_READ_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = readError.isEmpty()
                    ? QStringLiteral("failed to read file")
                    : readError;
            }
            return false;
        }

        *payload = readResult;
        return true;
    }

    if ( command == QStringLiteral("file.write") )
    {
        QJsonObject writeResult;
        QString writeError;
        bool invalidParams = false;
        if ( !FileWriteAccess::write(params, &writeResult, &writeError, &invalidParams) )
        {
            if ( errorCode != nullptr )
            {
                *errorCode = invalidParams
                    ? QStringLiteral("INVALID_PARAMS")
                    : QStringLiteral("FILE_WRITE_FAILED");
            }
            if ( errorMessage != nullptr )
            {
                *errorMessage = writeError.isEmpty()
                    ? QStringLiteral("failed to write file")
                    : writeError;
            }
            return false;
        }

        *payload = writeResult;
        return true;
    }

    if ( errorCode != nullptr )
    {
        *errorCode = QStringLiteral("COMMAND_NOT_SUPPORTED");
    }
    if ( errorMessage != nullptr )
    {
        *errorMessage = QStringLiteral("unsupported invoke command: %1").arg(command);
    }
    return false;
}

void NodeApplication::sendInvokeSuccess(
    const QString &invokeId,
    const QString &nodeId,
    const QJsonValue &payload
)
{
    QJsonObject params;
    params.insert(QStringLiteral("id"), invokeId);
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("ok"), true);
    if ( !payload.isUndefined() )
    {
        QString payloadJson;
        if ( trySerializeJsonValue(payload, &payloadJson) )
        {
            params.insert(QStringLiteral("payloadJSON"), payloadJson);
        }
        else
        {
            params.insert(QStringLiteral("payload"), payload);
        }
    }
    gatewayClient_.sendInvokeResult(params);
}

void NodeApplication::sendInvokeError(
    const QString &invokeId,
    const QString &nodeId,
    const QString &code,
    const QString &message
)
{
    QJsonObject errorObject;
    const QString normalizedCode = code.trimmed();
    const QString normalizedMessage = message.trimmed().isEmpty()
        ? QStringLiteral("invoke command failed")
        : message.trimmed();

    if ( !normalizedCode.isEmpty() )
    {
        errorObject.insert(QStringLiteral("code"), normalizedCode);
    }
    errorObject.insert(QStringLiteral("message"), normalizedMessage);

    QJsonObject params;
    params.insert(QStringLiteral("id"), invokeId);
    params.insert(QStringLiteral("nodeId"), nodeId);
    params.insert(QStringLiteral("ok"), false);
    params.insert(QStringLiteral("error"), errorObject);
    gatewayClient_.sendInvokeResult(params);
}

void NodeApplication::sendConnectRequest(const QString &nonce)
{
    QString optionsError;
    const NodeOptions options = buildNodeOptions(&optionsError);
    if ( !optionsError.isEmpty() )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = optionsError.trimmed();
        updateConnectionStatusAction();
        qCritical().noquote() << optionsError;
        startPairingReconnect();
        qWarning().noquote() << QStringLiteral(
            "connect request aborted by invalid options"
        );
        return;
    }

    NodeRegistrar registrar(options);
    QJsonObject params;
    QString error;
    if ( !registrar.buildConnectParams(identity_, nonce, &params, &error) )
    {
        setConnectionState(ConnectionState::Error);
        connectionStateDetail_ = error.trimmed();
        updateConnectionStatusAction();
        qCritical().noquote() << error;
        startPairingReconnect();
        qWarning().noquote() << QStringLiteral(
            "connect request aborted by invalid connect params"
        );
        return;
    }

    gatewayClient_.sendConnect(params);
}

QString NodeApplication::parseErrorMessage(const QJsonObject &errorObject)
{
    const QString message = errorObject.value("message").toString().trimmed();
    if ( !message.isEmpty() )
    {
        return message;
    }

    const QJsonObject detailsObject = errorObject.value("details").toObject();
    const QString detailsMessage = detailsObject.value("message").toString().trimmed();
    if ( !detailsMessage.isEmpty() )
    {
        return detailsMessage;
    }

    const QString detailsCode = detailsObject.value("code").toString().trimmed();
    if ( !detailsCode.isEmpty() )
    {
        return detailsCode;
    }

    const QString detailsReason = detailsObject.value("reason").toString().trimmed();
    if ( !detailsReason.isEmpty() )
    {
        return detailsReason;
    }

    const QString code = errorObject.value("code").toString().trimmed();
    if ( !code.isEmpty() )
    {
        return code;
    }

    return QStringLiteral("unknown connect error");
}

bool NodeApplication::isPairingRequiredConnectError(const QJsonObject &errorObject)
{
    const QJsonObject detailsObject = errorObject.value("details").toObject();

    const QString detailsCode = detailsObject.value("code").toString().trimmed().toUpper();
    if ( detailsCode == QStringLiteral("PAIRING_REQUIRED") ||
         detailsCode == QStringLiteral("NOT_PAIRED") )
    {
        return true;
    }

    const QString detailsReason = detailsObject.value("reason").toString().trimmed().toLower();
    if ( detailsReason == QStringLiteral("pairing_required") ||
         detailsReason == QStringLiteral("not_paired") )
    {
        return true;
    }

    const QString detailsMessage = detailsObject.value("message").toString().trimmed().toLower();
    if ( detailsMessage.contains(QStringLiteral("pairing required")) ||
         detailsMessage.contains(QStringLiteral("not paired")) )
    {
        return true;
    }

    const QString code = errorObject.value("code").toString().trimmed().toUpper();
    if ( code == QStringLiteral("PAIRING_REQUIRED") ||
         code == QStringLiteral("NOT_PAIRED") )
    {
        return true;
    }

    const QString message = errorObject.value("message").toString().trimmed().toLower();
    return message.contains(QStringLiteral("pairing required")) ||
        message.contains(QStringLiteral("not paired"));
}

