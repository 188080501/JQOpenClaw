// .h include
#include "common/common.h"

// Qt lib import
#include <QCryptographicHash>
#include <QFile>
#include <QJsonValue>

// OpenSSL lib import
#include <openssl/err.h>

QJsonArray Common::toJsonArray(const QStringList &items)
{
    QJsonArray out;
    for ( const QString &item : items )
    {
        out.append(item);
    }
    return out;
}

QString Common::processExitStatusName(QProcess::ExitStatus exitStatus)
{
    if ( exitStatus == QProcess::NormalExit )
    {
        return QStringLiteral("normal");
    }
    return QStringLiteral("crash");
}

QString Common::processErrorName(QProcess::ProcessError processError)
{
    switch ( processError )
    {
    case QProcess::FailedToStart:
        return QStringLiteral("failed_to_start");
    case QProcess::Crashed:
        return QStringLiteral("crashed");
    case QProcess::Timedout:
        return QStringLiteral("timed_out");
    case QProcess::ReadError:
        return QStringLiteral("read_error");
    case QProcess::WriteError:
        return QStringLiteral("write_error");
    case QProcess::UnknownError:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

bool Common::hasProcessError(QProcess::ProcessError processError)
{
    return processError != QProcess::UnknownError;
}

QString Common::processResultClass(
    bool timedOut,
    QProcess::ExitStatus exitStatus,
    int exitCode
)
{
    if ( timedOut )
    {
        return QStringLiteral("timeout");
    }
    if ( exitStatus != QProcess::NormalExit )
    {
        return QStringLiteral("crash");
    }
    if ( exitCode != 0 )
    {
        return QStringLiteral("non_zero_exit");
    }
    return QStringLiteral("ok");
}

Qt::CaseSensitivity Common::pathCaseSensitivity()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // Keep path comparisons aligned with default Windows/macOS filesystems.
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString Common::normalizeToken(const QString &value)
{
    QString normalized = value.trimmed().toLower();
    normalized.remove('-');
    normalized.remove('_');
    normalized.remove(' ');
    return normalized;
}

QString Common::extractStringRaw(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

QString Common::extractStringTrimmed(const QJsonObject &object, const QString &key)
{
    return extractStringRaw(object, key).trimmed();
}

bool Common::calculateFileMd5Hex(
    const QString &path,
    QString *md5Hex,
    QString *error,
    const QString &errorScope
)
{
    const QString normalizedErrorScope = errorScope.trimmed();
    const auto scopedErrorText =
        [ normalizedErrorScope ](const QString &message)
        {
            if ( normalizedErrorScope.isEmpty() )
            {
                return message;
            }
            return QStringLiteral("%1 %2").arg(normalizedErrorScope, message);
        };

    if ( md5Hex == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedErrorText(QStringLiteral("md5 output pointer is null"));
        }
        return false;
    }

    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        if ( error != nullptr )
        {
            *error = scopedErrorText(
                QStringLiteral("failed to open file for md5: %1")
                    .arg(file.errorString().trimmed())
            );
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
                *error = scopedErrorText(
                    QStringLiteral("failed to read file for md5: %1")
                        .arg(file.errorString().trimmed())
                );
            }
            return false;
        }
        md5.addData(block);
    }

    *md5Hex = QString::fromLatin1(md5.result().toHex()).toLower();
    return true;
}

QString Common::lastOpenSslError()
{
    const unsigned long errorCode = ERR_get_error();
    if ( errorCode == 0UL )
    {
        return QStringLiteral("unknown OpenSSL error");
    }

    char buffer[256] = {0};
    ERR_error_string_n(errorCode, buffer, sizeof(buffer));
    return QString::fromLatin1(buffer);
}

bool Common::parseEncoding(
    const QJsonObject &paramsObject,
    const QString &field,
    ContentEncoding *encoding,
    QString *error
)
{
    if ( encoding == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("internal error: encoding output pointer is null");
        }
        return false;
    }

    *encoding = ContentEncoding::Utf8;
    const QJsonValue value = paramsObject.value(field);
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }
    if ( !value.isString() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("%1 must be string").arg(field);
        }
        return false;
    }

    const QString normalized = normalizeToken(value.toString());
    if ( normalized.isEmpty() || ( normalized == QStringLiteral("utf8") ) )
    {
        *encoding = ContentEncoding::Utf8;
        return true;
    }
    if ( normalized == QStringLiteral("base64") )
    {
        *encoding = ContentEncoding::Base64;
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("%1 must be utf8 or base64").arg(field);
    }
    return false;
}

bool Common::parseOptionalBool(
    const QJsonObject &paramsObject,
    const QString &field,
    bool defaultValue,
    bool *out,
    QString *error
)
{
    if ( out == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("internal error: bool output pointer is null");
        }
        return false;
    }

    *out = defaultValue;
    const QJsonValue value = paramsObject.value(field);
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }
    if ( !value.isBool() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("%1 must be boolean").arg(field);
        }
        return false;
    }

    *out = value.toBool();
    return true;
}

QString Common::encodingName(ContentEncoding encoding)
{
    if ( encoding == ContentEncoding::Base64 )
    {
        return QStringLiteral("base64");
    }
    return QStringLiteral("utf8");
}
