// .h include
#include "openclawprotocol/gatewayclient.h"

// Qt lib import
#include <QCryptographicHash>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QStringList>
#include <QUuid>
#include <QUrl>
#include <QWebSocketProtocol>

// JQOpenClaw import
#include "crypto/cryptoencoding.h"

namespace
{
QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

bool shouldLogEvent(const QString &eventName)
{
    return eventName == QStringLiteral("connect.challenge")
        || eventName == QStringLiteral("node.invoke.request");
}
}

GatewayClient::GatewayClient(QObject *parent) :
    QObject(parent)
{
    connect(&socket_, &QWebSocket::connected, this, &GatewayClient::onConnected);
    connect(&socket_, &QWebSocket::disconnected, this, &GatewayClient::onDisconnected);
    connect(&socket_, &QWebSocket::errorOccurred, this, &GatewayClient::onErrorOccurred);
    connect(
        &socket_,
        &QWebSocket::textMessageReceived,
        this,
        &GatewayClient::onTextMessageReceived
    );
    connect(&socket_, &QWebSocket::sslErrors, this, &GatewayClient::onSslErrors);
}

void GatewayClient::setOptions(const NodeOptions &options)
{
    options_ = options;
}

void GatewayClient::open()
{
    const QString urlText = gatewayUrl();
    const QUrl url(urlText);
    if ( !url.isValid() )
    {
        emit transportError(QStringLiteral("invalid gateway url: %1").arg(urlText));
        return;
    }
    socket_.open(url);
}

void GatewayClient::close()
{
    if ( socket_.state() == QAbstractSocket::ConnectedState
        || socket_.state() == QAbstractSocket::ConnectingState )
        {
        socket_.close();
    }
}

bool GatewayClient::isOpen() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void GatewayClient::sendConnect(const QJsonObject &params)
{
    if ( !isOpen() )
    {
        emit transportError(QStringLiteral("gateway socket is not connected"));
        return;
    }

    pendingConnectRequestId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("req"));
    request.insert(QStringLiteral("id"), pendingConnectRequestId_);
    request.insert(QStringLiteral("method"), QStringLiteral("connect"));
    request.insert(QStringLiteral("params"), params);

    const QString payload = QString::fromUtf8(
        QJsonDocument(request).toJson(QJsonDocument::Compact)
    );
    socket_.sendTextMessage(payload);
}

void GatewayClient::sendInvokeResult(const QJsonObject &params)
{
    if ( !isOpen() )
    {
        emit transportError(QStringLiteral("gateway socket is not connected"));
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("req"));
    request.insert(
        QStringLiteral("id"),
        QUuid::createUuid().toString(QUuid::WithoutBraces)
    );
    request.insert(QStringLiteral("method"), QStringLiteral("node.invoke.result"));
    request.insert(QStringLiteral("params"), params);

    const QString payload = QString::fromUtf8(
        QJsonDocument(request).toJson(QJsonDocument::Compact)
    );
    socket_.sendTextMessage(payload);
}

void GatewayClient::onConnected()
{
    if ( options_.tls
        && !options_.tlsFingerprint.trimmed().isEmpty() )
        {
        const QSslCertificate peerCertificate = socket_.sslConfiguration().peerCertificate();
        if ( peerCertificate.isNull() )
        {
            emit transportError(QStringLiteral("tls peer certificate is missing"));
            socket_.close(
                QWebSocketProtocol::CloseCodePolicyViolated,
                QStringLiteral("tls cert missing")
            );
            return;
        }

        const QString actualFingerprint = QString::fromLatin1(
            peerCertificate.digest(QCryptographicHash::Sha256).toHex()
        );
        const QString expectedFingerprint = CryptoEncoding::normalizeFingerprint(
            options_.tlsFingerprint
        );
        if ( CryptoEncoding::normalizeFingerprint(actualFingerprint) != expectedFingerprint )
        {
            emit transportError(QStringLiteral("tls fingerprint mismatch"));
            socket_.close(
                QWebSocketProtocol::CloseCodePolicyViolated,
                QStringLiteral("tls fingerprint mismatch")
            );
            return;
        }
    }

    emit opened();
}

void GatewayClient::onDisconnected()
{
    pendingConnectRequestId_.clear();
    emit closed();
}

void GatewayClient::onErrorOccurred(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);

    const QString message = socket_.errorString().trimmed();
    if ( message.isEmpty() )
    {
        emit transportError(QStringLiteral("gateway transport error"));
        return;
    }
    emit transportError(QStringLiteral("gateway transport error: %1").arg(message));
}

void GatewayClient::onTextMessageReceived(const QString &message)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if ( parseError.error != QJsonParseError::NoError
        || !doc.isObject() )
        {
        emit transportError(
            QStringLiteral("invalid gateway message: %1").arg(parseError.errorString())
        );
        return;
    }

    const QJsonObject root = doc.object();
    const QString frameType = extractString(root, "type");
    if ( frameType == "event" )
    {
        const QString eventName = extractString(root, "event");
        if ( shouldLogEvent(eventName) )
        {
            qInfo().noquote() << QStringLiteral("[gateway.rx.event] %1").arg(eventName);
        }
        if ( eventName == "connect.challenge" )
        {
            const QJsonObject payload = root.value("payload").toObject();
            const QString nonce = extractString(payload, "nonce").trimmed();
            if ( nonce.isEmpty() )
            {
                emit transportError(QStringLiteral("connect challenge nonce is missing"));
                return;
            }
            emit challengeReceived(nonce);
            return;
        }
        if ( eventName == "node.invoke.request" )
        {
            const QJsonObject payload = root.value("payload").toObject();
            const QString invokeId = extractString(payload, "id").trimmed();
            const QString nodeId = extractString(payload, "nodeId").trimmed();
            const QString command = extractString(payload, "command").trimmed();
            if ( invokeId.isEmpty() || nodeId.isEmpty() || command.isEmpty() )
            {
                qWarning().noquote() << QStringLiteral(
                    "invalid node.invoke.request event: missing id/nodeId/command"
                );
                return;
            }
            emit invokeRequestReceived(payload);
        }
        return;
    }

    if ( frameType != "res" )
    {
        return;
    }

    const QString responseId = extractString(root, "id");
    if ( pendingConnectRequestId_.isEmpty()
        || responseId != pendingConnectRequestId_ )
        {
        return;
    }
    pendingConnectRequestId_.clear();

    const bool ok = root.value("ok").toBool(false);
    qInfo().noquote() << QStringLiteral("[gateway.rx.res] id=%1 ok=%2")
                             .arg(responseId, ok ? QStringLiteral("true") : QStringLiteral("false"));
    if ( ok )
    {
        emit connectAccepted(root.value("payload").toObject());
    }
    else
    {
        emit connectRejected(root.value("error").toObject());
    }
}

void GatewayClient::onSslErrors(const QList<QSslError> &errors)
{
    if ( !options_.tlsFingerprint.trimmed().isEmpty() )
    {
        socket_.ignoreSslErrors();
        return;
    }

    QStringList errorTexts;
    for ( const QSslError &error : errors )
    {
        errorTexts.append(error.errorString());
    }
    emit transportError(QStringLiteral("tls validation failed: %1").arg(errorTexts.join("; ")));
}

QString GatewayClient::gatewayUrl() const
{
    const QString scheme = options_.tls ? QStringLiteral("wss") : QStringLiteral("ws");
    return QStringLiteral("%1://%2:%3").arg(scheme, options_.host, QString::number(options_.port));
}
