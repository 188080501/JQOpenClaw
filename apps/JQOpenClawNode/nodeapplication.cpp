// .h include
#include "nodeapplication.h"

// Qt lib import
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QUrl>

// JQOpenClaw import
#include "capabilities/system/systemscreenshot.h"
#include "capabilities/system/systeminfo.h"
#include "crypto/secretbox/secretboxcrypto.h"
#include "crypto/signing/deviceauth.h"

namespace
{
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
    const QString &baseUri,
    const QString &segment,
    const QString &fileName
)
{
    QUrl url(baseUri.trimmed());
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
    const QString &fileServerUri,
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

    const QString normalizedUri = fileServerUri.trimmed();
    if ( normalizedUri.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file server uri is empty");
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
    const QUrl uploadUrl = buildFileServerUrl(normalizedUri, QStringLiteral("upload"), fileName);
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
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec();

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

    if ( statusCode < 200
        || statusCode >= 300 )
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

    const QUrl fileAccessUrl = buildFileServerUrl(normalizedUri, QStringLiteral("files"), fileName);
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
}

NodeApplication::NodeApplication(const NodeOptions &options, QObject *parent) :
    QObject(parent),
    options_(options),
    gatewayClient_(this),
    registrar_(options)
{
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
}

void NodeApplication::start()
{
    QString initError;
    if ( !DeviceAuth::initialize(&initError) )
    {
        qCritical().noquote() << initError;
        emit finished(1);
        return;
    }

    DeviceIdentityStore store(options_.identityPath);
    QString identityError;
    if ( !store.loadOrCreate(&identity_, &identityError) )
    {
        qCritical().noquote() << identityError;
        emit finished(1);
        return;
    }

    QString selfTestError;
    if ( !runCryptoSelfTest(&selfTestError) )
    {
        qCritical().noquote() << selfTestError;
        emit finished(1);
        return;
    }

    qInfo().noquote() << QStringLiteral("device identity: %1").arg(identity_.deviceId);
    qInfo().noquote() << QStringLiteral("identity file: %1").arg(store.identityPath());

    gatewayClient_.setOptions(options_);
    gatewayClient_.open();
}

void NodeApplication::onChallengeReceived(const QString &nonce)
{
    sendConnectRequest(nonce);
}

void NodeApplication::onConnectAccepted(const QJsonObject &payload)
{
    registered_ = true;

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

    if ( options_.exitAfterRegister )
    {
        emit finished(0);
    }
}

void NodeApplication::onConnectRejected(const QJsonObject &error)
{
    const QString message = parseErrorMessage(error);
    qCritical().noquote() << QStringLiteral("gateway connect rejected: %1").arg(message);
    emit finished(2);
}

void NodeApplication::onInvokeRequestReceived(const QJsonObject &payload)
{
    const QString invokeId = extractString(payload, QStringLiteral("id"));
    const QString nodeId = extractString(payload, QStringLiteral("nodeId"));
    const QString command = extractString(payload, QStringLiteral("command"));
    QString paramsJson = extractString(payload, QStringLiteral("paramsJSON"));
    if ( paramsJson.isEmpty() )
    {
        QString serializedParams;
        if ( trySerializeJsonValue(payload.value(QStringLiteral("params")), &serializedParams) )
        {
            paramsJson = serializedParams;
        }
    }

    qInfo().noquote() << QStringLiteral(
        "[node.invoke] request received id=%1 nodeId=%2 command=%3 paramsJSON=%4"
    ).arg(invokeId, nodeId, command, paramsJson);

    if ( invokeId.isEmpty() || nodeId.isEmpty() || command.isEmpty() )
    {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] request ignored: missing id/nodeId/command"
        );
        return;
    }

    QJsonValue params = QJsonObject();
    QString parseError;
    if ( !parseInvokeParamsJson(paramsJson, &params, &parseError) )
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

    QJsonValue responsePayload;
    QString errorCode;
    QString errorMessage;
    if ( !executeInvokeCommand(
            command,
            params,
            &responsePayload,
            &errorCode,
            &errorMessage
        ) )
        {
        qWarning().noquote() << QStringLiteral(
            "[node.invoke] command failed id=%1 command=%2 code=%3 message=%4"
        ).arg(invokeId, command, errorCode, errorMessage);
        sendInvokeError(invokeId, nodeId, errorCode, errorMessage);
        return;
    }

    sendInvokeSuccess(invokeId, nodeId, responsePayload);
    qInfo().noquote() << QStringLiteral(
        "[node.invoke] command done id=%1 command=%2"
    ).arg(invokeId, command);
}

void NodeApplication::onTransportError(const QString &message)
{
    qCritical().noquote() << message;
    if ( !registered_ )
    {
        emit finished(1);
    }
}

void NodeApplication::onGatewayClosed()
{
    if ( !registered_ )
    {
        emit finished(1);
        return;
    }

    if ( !options_.exitAfterRegister )
    {
        emit finished(3);
    }
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
    const QString &paramsJson,
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

    const QString normalized = paramsJson.trimmed();
    if ( normalized.isEmpty() )
    {
        *params = QJsonObject();
        return true;
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
    if ( json.isArray() )
    {
        *params = json.array();
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("paramsJSON must be object or array");
    }
    return false;
}

bool NodeApplication::executeInvokeCommand(
    const QString &command,
    const QJsonValue &params,
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

    if ( command == QStringLiteral("system.info") )
    {
        Q_UNUSED(params);
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
        Q_UNUSED(params);
        QList<SystemScreenshot::CaptureResult> captures;
        QString captureError;
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
                    options_.fileServerUri,
                    options_.fileServerToken,
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
    QJsonObject params;
    QString error;
    if ( !registrar_.buildConnectParams(identity_, nonce, &params, &error) )
    {
        qCritical().noquote() << error;
        emit finished(1);
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
    return QStringLiteral("unknown connect error");
}

