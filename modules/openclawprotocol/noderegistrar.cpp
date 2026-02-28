// .h include
#include "openclawprotocol/noderegistrar.h"

// Qt lib import
#include <QDateTime>
#include <QJsonObject>
#include <QLocale>
#include <QSysInfo>

// JQOpenClaw import
#include "crypto/cryptoencoding.h"
#include "crypto/signing/deviceauth.h"
#include "openclawprotocol/nodeprofile.h"

NodeRegistrar::NodeRegistrar(const NodeOptions &options) :
    options_(options)
{
}

bool NodeRegistrar::buildConnectParams(
    const DeviceIdentity &identity,
    const QString &challengeNonce,
    QJsonObject *params,
    QString *error
) const
{
    if ( params == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("connect params output pointer is null");
        }
        return false;
    }

    const QString nonce = challengeNonce.trimmed();
    if ( nonce.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("challenge nonce is empty");
        }
        return false;
    }

    const QString token = options_.token.trimmed();
    const qint64 signedAtMs = QDateTime::currentMSecsSinceEpoch();
    const QString role = QStringLiteral("node");
    const QStringList scopes;

    DeviceAuthPayloadInput payloadInput;
    payloadInput.deviceId = identity.deviceId;
    payloadInput.clientId = NodeProfile::clientId();
    payloadInput.clientMode = QStringLiteral("node");
    payloadInput.role = role;
    payloadInput.scopes = scopes;
    payloadInput.signedAtMs = signedAtMs;
    payloadInput.token = token;
    payloadInput.nonce = nonce;
    payloadInput.platform = platformName();
    payloadInput.deviceFamily = options_.deviceFamily;

    const QString payload = DeviceAuth::buildPayloadV3(payloadInput);

    QString signature;
    QString signatureError;
    if ( !DeviceAuth::signDetached(
            identity.secretKey,
            payload.toUtf8(),
            &signature,
            &signatureError
        ) )
        {
        if ( error != nullptr )
        {
            *error = signatureError;
        }
        return false;
    }

    QJsonObject deviceObject;
    deviceObject.insert("id", identity.deviceId);
    deviceObject.insert("publicKey", CryptoEncoding::toBase64Url(identity.publicKey));
    deviceObject.insert("signature", signature);
    deviceObject.insert("signedAt", signedAtMs);
    deviceObject.insert("nonce", nonce);

    QJsonObject clientObject;
    clientObject.insert("id", NodeProfile::clientId());
    clientObject.insert("version", NodeProfile::clientVersion());
    clientObject.insert("platform", platformName());
    clientObject.insert("mode", QStringLiteral("node"));
    clientObject.insert("deviceFamily", options_.deviceFamily);
    if ( !options_.displayName.trimmed().isEmpty() )
    {
        clientObject.insert("displayName", options_.displayName.trimmed());
    }
    if ( !options_.nodeId.trimmed().isEmpty() )
    {
        clientObject.insert("instanceId", options_.nodeId.trimmed());
    }

    QJsonObject connectParams;
    connectParams.insert("minProtocol", NodeProfile::minProtocolVersion());
    connectParams.insert("maxProtocol", NodeProfile::maxProtocolVersion());
    connectParams.insert("client", clientObject);
    connectParams.insert("role", role);
    connectParams.insert("scopes", QJsonArray());
    connectParams.insert("caps", NodeProfile::caps());
    connectParams.insert("commands", NodeProfile::commands());
    connectParams.insert("permissions", NodeProfile::permissions());
    connectParams.insert("locale", QLocale::system().name().replace('_', '-'));
    connectParams.insert(
        "userAgent",
        QStringLiteral("jqopenclaw-node/%1").arg(NodeProfile::clientVersion())
    );
    connectParams.insert("device", deviceObject);

    if ( !token.isEmpty() )
    {
        QJsonObject auth;
        auth.insert(QStringLiteral("token"), token);
        connectParams.insert("auth", auth);
    }

    *params = connectParams;
    return true;
}

QString NodeRegistrar::platformName()
{
    const QString platform = QSysInfo::productType().trimmed();
    if ( platform.isEmpty() )
    {
        return QStringLiteral("windows");
    }
    return CryptoEncoding::normalizeMetadataForAuth(platform);
}
