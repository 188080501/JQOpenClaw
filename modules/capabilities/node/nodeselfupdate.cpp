// .h include
#include "capabilities/node/nodeselfupdate.h"

// Qt lib import
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QUrl>

namespace
{
const int selfUpdateDownloadTimeoutMs = 5 * 60 * 1000;

QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

QString calculateMd5Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex()
    ).toLower();
}

bool calculateFileMd5Hex(
    const QString &path,
    QString *md5Hex,
    QString *error
)
{
    if ( md5Hex == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate internal error: md5 output pointer is null");
        }
        return false;
    }

    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate failed to open file for md5: %1")
                .arg(file.errorString());
        }
        return false;
    }

    QCryptographicHash md5(QCryptographicHash::Md5);
    while ( !file.atEnd() )
    {
        const QByteArray block = file.read(64 * 1024);
        if ( block.isEmpty() && ( file.error() != QFile::NoError ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("node.selfUpdate failed to read file for md5: %1")
                    .arg(file.errorString());
            }
            return false;
        }
        md5.addData(block);
    }

    *md5Hex = QString::fromLatin1(md5.result().toHex()).toLower();
    return true;
}

bool isValidMd5Hex(const QString &value)
{
    const QString normalized = value.trimmed();
    if ( normalized.size() != 32 )
    {
        return false;
    }

    for ( const QChar &ch : normalized )
    {
        if ( !ch.isDigit() &&
             ( ch.toLower() < QLatin1Char('a') || ch.toLower() > QLatin1Char('f') ) )
        {
            return false;
        }
    }
    return true;
}

bool writeBytesToFile(
    const QString &path,
    const QByteArray &bytes,
    QString *error
)
{
    QSaveFile saveFile(path);
    if ( !saveFile.open(QIODevice::WriteOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate failed to open file for writing: %1")
                .arg(saveFile.errorString());
        }
        return false;
    }

    const qint64 written = saveFile.write(bytes);
    if ( written != bytes.size() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate failed to write file bytes");
        }
        saveFile.cancelWriting();
        return false;
    }

    if ( !saveFile.commit() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate failed to commit file: %1")
                .arg(saveFile.errorString());
        }
        return false;
    }

    return true;
}

bool downloadBinaryByHttp(
    const QUrl &url,
    int timeoutMs,
    QByteArray *bytes,
    QString *error
)
{
    if ( bytes == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate internal error: output pointer is null");
        }
        return false;
    }

    QNetworkRequest request(url);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy
    );

    QNetworkAccessManager networkAccessManager;
    QNetworkReply *reply = networkAccessManager.get(request);

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
    timeoutTimer.start(timeoutMs);
    eventLoop.exec();
    timeoutTimer.stop();

    if ( requestTimedOut )
    {
        reply->deleteLater();
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate download timed out after %1ms")
                .arg(QString::number(timeoutMs));
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
            *error = QStringLiteral("node.selfUpdate download network error (%1): %2")
                .arg(static_cast<int>(networkError))
                .arg(
                    networkErrorText.isEmpty()
                        ? QStringLiteral("unknown network error")
                        : networkErrorText
                );
        }
        return false;
    }

    if ( ( statusCode < 200 ) || ( statusCode >= 300 ) )
    {
        if ( error != nullptr )
        {
            const QString bodyText = QString::fromUtf8(responseBody).trimmed().left(200);
            if ( bodyText.isEmpty() )
            {
                *error = QStringLiteral("node.selfUpdate download failed with status code %1")
                    .arg(QString::number(statusCode));
            }
            else
            {
                *error = QStringLiteral("node.selfUpdate download failed with status code %1: %2")
                    .arg(QString::number(statusCode), bodyText);
            }
        }
        return false;
    }

    if ( responseBody.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate downloaded data is empty");
        }
        return false;
    }

    *bytes = responseBody;
    return true;
}

QString buildSelfUpdateBatchContent(
    const QString &sourceFilePath,
    const QString &targetFilePath
)
{
    QStringList scriptLines{
        "@echo off",
        "setlocal enableextensions enabledelayedexpansion",
        QString("set \"source=%1\"").arg(QDir::toNativeSeparators(sourceFilePath)),
        QString("set \"target=%1\"").arg(QDir::toNativeSeparators(targetFilePath)),
        "set \"script=%~f0\"",
        "for /l %%i in (1,1,30) do (",
        "    copy /y \"%source%\" \"%target%\" >nul 2>nul",
        "    if not errorlevel 1 goto success",
        "    ping 127.0.0.1 -n 2 >nul",
        ")",
        "exit /b 1",
        ":success"
    };

    scriptLines.append(QString("\"%1\"").arg(QDir::toNativeSeparators(targetFilePath)));
    scriptLines.append("del \"%source%\" >nul 2>nul");
    scriptLines.append("del \"%script%\" >nul 2>nul");
    scriptLines.append("exit /b 0");

    QString scriptContent = scriptLines.join("\r\n");
    scriptContent.append("\r\n");
    return scriptContent;
}
}

bool NodeSelfUpdate::execute(
    const QJsonValue &params,
    QJsonObject *result,
    QString *error,
    bool *invalidParams,
    bool *md5Mismatch
)
{
    if ( invalidParams != nullptr )
    {
        *invalidParams = false;
    }
    if ( md5Mismatch != nullptr )
    {
        *md5Mismatch = false;
    }
    if ( result == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate output pointer is null");
        }
        return false;
    }

#if !defined(Q_OS_WIN)
    Q_UNUSED(params)
    if ( error != nullptr )
    {
        *error = QStringLiteral("node.selfUpdate is only supported on Windows");
    }
    return false;
#endif

    if ( !params.isObject() )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate params must be object");
        }
        return false;
    }

    const QJsonObject paramsObject = params.toObject();
    QString downloadUrlText = extractString(paramsObject, QStringLiteral("downloadUrl"));
    if ( downloadUrlText.isEmpty() )
    {
        downloadUrlText = extractString(paramsObject, QStringLiteral("url"));
    }
    if ( downloadUrlText.isEmpty() )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate requires downloadUrl");
        }
        return false;
    }

    const QUrl downloadUrl(downloadUrlText);
    const QString downloadScheme = downloadUrl.scheme().trimmed().toLower();
    if ( !downloadUrl.isValid() ||
         downloadUrl.host().trimmed().isEmpty() ||
         ( downloadScheme != QStringLiteral("http") &&
           downloadScheme != QStringLiteral("https") ) )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate downloadUrl must be valid http/https url");
        }
        return false;
    }

    const QString expectedMd5 = extractString(paramsObject, QStringLiteral("md5")).toLower();
    if ( !expectedMd5.isEmpty() && !isValidMd5Hex(expectedMd5) )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate md5 must be 32-char hex");
        }
        return false;
    }

    const QString appPath = QCoreApplication::applicationFilePath().trimmed();
    if ( appPath.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate failed to resolve current application path");
        }
        return false;
    }

    QString currentMd5;
    QString currentMd5Error;
    if ( !expectedMd5.isEmpty() &&
         !calculateFileMd5Hex(appPath, &currentMd5, &currentMd5Error) )
    {
        if ( error != nullptr )
        {
            *error = currentMd5Error;
        }
        return false;
    }

    if ( !expectedMd5.isEmpty() && ( currentMd5 == expectedMd5 ) )
    {
        QJsonObject out;
        out.insert(QStringLiteral("operation"), QStringLiteral("selfUpdate"));
        out.insert(QStringLiteral("updated"), false);
        out.insert(QStringLiteral("reason"), QStringLiteral("md5_unchanged"));
        out.insert(QStringLiteral("currentMd5"), currentMd5);
        out.insert(QStringLiteral("expectedMd5"), expectedMd5);
        *result = out;
        return true;
    }

    QByteArray downloadedBytes;
    QString downloadError;
    if ( !downloadBinaryByHttp(
            downloadUrl,
            selfUpdateDownloadTimeoutMs,
            &downloadedBytes,
            &downloadError
        ) )
    {
        if ( error != nullptr )
        {
            *error = downloadError;
        }
        return false;
    }

    const QString downloadedMd5 = calculateMd5Hex(downloadedBytes);
    if ( !expectedMd5.isEmpty() && ( downloadedMd5 != expectedMd5 ) )
    {
        if ( md5Mismatch != nullptr )
        {
            *md5Mismatch = true;
        }
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "node.selfUpdate downloaded md5 mismatch: expected=%1 actual=%2"
            ).arg(expectedMd5, downloadedMd5);
        }
        return false;
    }

    QDir tempDirectory(QDir::tempPath());
    if ( !tempDirectory.exists() && !tempDirectory.mkpath(QStringLiteral(".")) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate failed to create temp directory");
        }
        return false;
    }

    const QString tempSourcePath = tempDirectory.filePath(
        QUuid::createUuid().toString(QUuid::WithoutBraces)
    );
    QString writeError;
    if ( !writeBytesToFile(tempSourcePath, downloadedBytes, &writeError) )
    {
        if ( error != nullptr )
        {
            *error = writeError;
        }
        return false;
    }

    const QString tempScriptPath = tempDirectory.filePath(
        QStringLiteral("%1.bat").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
    );
    const QString scriptContent = buildSelfUpdateBatchContent(
        QDir::toNativeSeparators(tempSourcePath),
        QDir::toNativeSeparators(appPath)
    );
    if ( !writeBytesToFile(tempScriptPath, scriptContent.toUtf8(), &writeError) )
    {
        QFile::remove(tempSourcePath);
        if ( error != nullptr )
        {
            *error = writeError;
        }
        return false;
    }

    const QString scriptPath = QDir::toNativeSeparators(tempScriptPath);
    const bool scriptStarted = QProcess::startDetached(
        QStringLiteral("cmd.exe"),
        QStringList({QStringLiteral("/c"), scriptPath})
    );
    if ( !scriptStarted )
    {
        QFile::remove(tempSourcePath);
        QFile::remove(tempScriptPath);
        if ( error != nullptr )
        {
            *error = QStringLiteral("node.selfUpdate failed to start update script");
        }
        return false;
    }

    QJsonObject out;
    out.insert(QStringLiteral("operation"), QStringLiteral("selfUpdate"));
    out.insert(QStringLiteral("updated"), true);
    out.insert(
        QStringLiteral("downloadUrl"),
        downloadUrl.toString(QUrl::FullyEncoded)
    );
    out.insert(QStringLiteral("downloadedMd5"), downloadedMd5);
    if ( !expectedMd5.isEmpty() )
    {
        out.insert(QStringLiteral("expectedMd5"), expectedMd5);
    }
    out.insert(QStringLiteral("willExit"), true);
    out.insert(QStringLiteral("status"), QStringLiteral("exiting_for_self_update"));
    *result = out;
    return true;
}
