// .h include
#include "common/common.h"

// Qt lib import
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QJsonValue>
#include <cmath>
#include <limits>

// OpenSSL lib import
#include <openssl/err.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
QString scopedMessage(const QString &scope, const QString &message)
{
    const QString normalizedScope = scope.trimmed();
    if ( normalizedScope.isEmpty() )
    {
        return message;
    }
    return QStringLiteral("%1 %2").arg(normalizedScope, message);
}

bool parseStringArrayField(
    const QJsonObject &paramsObject,
    const QString &field,
    bool required,
    QStringList *out,
    QString *error,
    const QString &scope
)
{
    if ( out == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: %1 output pointer is null").arg(field)
            );
        }
        return false;
    }

    out->clear();
    const QJsonValue value = paramsObject.value(field);
    if ( value.isUndefined() || value.isNull() )
    {
        if ( required )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("requires %1").arg(field)
                );
            }
            return false;
        }
        return true;
    }
    if ( !value.isArray() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be string array").arg(field)
            );
        }
        return false;
    }

    const QJsonArray array = value.toArray();
    for ( int index = 0; index < array.size(); ++index )
    {
        const QJsonValue item = array.at(index);
        if ( !item.isString() )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1[%2] must be string").arg(field).arg(index)
                );
            }
            return false;
        }
        out->append(item.toString());
    }
    return true;
}
}

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

bool Common::parseOptionalStringArray(
    const QJsonObject &paramsObject,
    const QString &field,
    QStringList *out,
    QString *error,
    const QString &scope
)
{
    return parseStringArrayField(
        paramsObject,
        field,
        false,
        out,
        error,
        scope
    );
}

bool Common::parseRequiredStringArray(
    const QJsonObject &paramsObject,
    const QString &field,
    QStringList *out,
    QString *error,
    const QString &scope
)
{
    return parseStringArrayField(
        paramsObject,
        field,
        true,
        out,
        error,
        scope
    );
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

QString Common::win32ErrorMessage(quint32 errorCode)
{
#ifdef Q_OS_WIN
    LPWSTR buffer = nullptr;
    const DWORD windowsErrorCode = static_cast<DWORD>(errorCode);
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        windowsErrorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );
    if ( ( size == 0 ) || ( buffer == nullptr ) )
    {
        return QStringLiteral("win32 error %1").arg(QString::number(errorCode));
    }

    const QString message = QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed();
    LocalFree(buffer);
    if ( message.isEmpty() )
    {
        return QStringLiteral("win32 error %1").arg(QString::number(errorCode));
    }
    return QStringLiteral("%1 (code=%2)")
        .arg(message, QString::number(errorCode));
#else
    return QStringLiteral("win32 error %1").arg(QString::number(errorCode));
#endif
}

bool Common::parseTimeoutMs(
    const QJsonObject &paramsObject,
    const QString &field,
    int defaultValue,
    int minValue,
    int maxValue,
    int *timeoutMs,
    QString *error,
    const QString &scope
)
{
    if ( timeoutMs == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: timeout output pointer is null")
            );
        }
        return false;
    }

    *timeoutMs = defaultValue;
    const QJsonValue timeoutValue = paramsObject.value(field);
    if ( timeoutValue.isUndefined() || timeoutValue.isNull() )
    {
        return true;
    }

    if ( !timeoutValue.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be number").arg(field)
            );
        }
        return false;
    }

    bool ok = false;
    const int parsedTimeoutMs = QString::number(timeoutValue.toDouble(), 'g', 16).toInt(&ok);
    if ( !ok )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 is invalid").arg(field)
            );
        }
        return false;
    }
    if ( ( parsedTimeoutMs < minValue ) ||
         ( parsedTimeoutMs > maxValue ) )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 out of range [%2, %3]")
                    .arg(field, QString::number(minValue), QString::number(maxValue))
            );
        }
        return false;
    }

    *timeoutMs = parsedTimeoutMs;
    return true;
}

bool Common::parseIntValue(
    const QJsonValue &rawValue,
    const QString &field,
    int minValue,
    int maxValue,
    int *value,
    QString *error,
    IntegerParseStyle style,
    const QString &scope
)
{
    if ( value == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: integer output pointer is null")
            );
        }
        return false;
    }

    if ( !rawValue.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be %2")
                    .arg(
                        field,
                        ( style == IntegerParseStyle::Integer )
                            ? QStringLiteral("integer")
                            : QStringLiteral("number")
                    )
            );
        }
        return false;
    }

    int parsedValue = 0;
    if ( style == IntegerParseStyle::Integer )
    {
        const double doubleValue = rawValue.toDouble(std::numeric_limits<double>::quiet_NaN());
        if ( !std::isfinite(doubleValue) )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 must be integer").arg(field)
                );
            }
            return false;
        }

        if ( ( doubleValue < static_cast<double>((std::numeric_limits<int>::min)()) ) ||
             ( doubleValue > static_cast<double>((std::numeric_limits<int>::max)()) ) )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 out of range").arg(field)
                );
            }
            return false;
        }

        parsedValue = static_cast<int>(doubleValue);
        if ( doubleValue != static_cast<double>(parsedValue) )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 must be integer").arg(field)
                );
            }
            return false;
        }
    }
    else
    {
        bool ok = false;
        parsedValue = QString::number(rawValue.toDouble(), 'g', 16).toInt(&ok);
        if ( !ok )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 is invalid").arg(field)
                );
            }
            return false;
        }
    }

    if ( ( parsedValue < minValue ) ||
         ( parsedValue > maxValue ) )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 out of range [%2, %3]")
                    .arg(field, QString::number(minValue), QString::number(maxValue))
            );
        }
        return false;
    }

    *value = parsedValue;
    return true;
}

bool Common::parseOptionalInt(
    const QJsonObject &paramsObject,
    const QString &field,
    int minValue,
    int maxValue,
    int defaultValue,
    int *value,
    QString *error,
    IntegerParseStyle style,
    const QString &scope
)
{
    if ( value == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: integer output pointer is null")
            );
        }
        return false;
    }

    *value = defaultValue;
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        return true;
    }

    return parseIntValue(rawValue, field, minValue, maxValue, value, error, style, scope);
}

bool Common::parseRequiredInt(
    const QJsonObject &paramsObject,
    const QString &field,
    int minValue,
    int maxValue,
    int *value,
    QString *error,
    IntegerParseStyle style,
    const QString &scope
)
{
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("requires %1").arg(field)
            );
        }
        return false;
    }

    return parseIntValue(rawValue, field, minValue, maxValue, value, error, style, scope);
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
    QString *error,
    const QString &scope
)
{
    if ( out == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: bool output pointer is null")
            );
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
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be boolean").arg(field)
            );
        }
        return false;
    }

    *out = value.toBool();
    return true;
}

bool Common::parseProcessEnvironment(
    const QJsonObject &paramsObject,
    const QString &environmentField,
    const QString &inheritField,
    bool defaultInheritEnvironment,
    QProcessEnvironment *environment,
    QString *error,
    const QString &scope,
    bool ignorePathOverride,
    const QSet<QString> *ignoredUpperKeys,
    const QString &warningPrefix
)
{
    if ( environment == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: environment output pointer is null")
            );
        }
        return false;
    }

    bool inheritEnvironment = defaultInheritEnvironment;
    if ( !inheritField.trimmed().isEmpty() )
    {
        if ( !parseOptionalBool(
                paramsObject,
                inheritField,
                defaultInheritEnvironment,
                &inheritEnvironment,
                error,
                scope
            ) )
        {
            return false;
        }
    }

    *environment = inheritEnvironment
        ? QProcessEnvironment::systemEnvironment()
        : QProcessEnvironment();

    const QJsonValue environmentValue = paramsObject.value(environmentField);
    if ( environmentValue.isUndefined() || environmentValue.isNull() )
    {
        return true;
    }
    if ( !environmentValue.isObject() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be object").arg(environmentField)
            );
        }
        return false;
    }

    const QJsonObject environmentObject = environmentValue.toObject();
    for ( auto it = environmentObject.constBegin(); it != environmentObject.constEnd(); ++it )
    {
        const QString key = it.key().trimmed();
        if ( key.isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 contains empty key").arg(environmentField)
                );
            }
            return false;
        }
        if ( !it.value().isString() )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 key \"%2\" must be string value")
                        .arg(environmentField, key)
                );
            }
            return false;
        }

        const QString normalizedKey = key.toUpper();
        if ( ignorePathOverride &&
             ( normalizedKey == QStringLiteral("PATH") ) )
        {
            if ( !warningPrefix.isEmpty() )
            {
                qWarning().noquote() << QStringLiteral(
                    "%1 ignore environment override key=%2"
                ).arg(
                    warningPrefix,
                    key
                );
            }
            continue;
        }
        if ( ( ignoredUpperKeys != nullptr ) &&
             ignoredUpperKeys->contains(normalizedKey) )
        {
            if ( !warningPrefix.isEmpty() )
            {
                qWarning().noquote() << QStringLiteral(
                    "%1 ignore unsafe environment key=%2"
                ).arg(
                    warningPrefix,
                    key
                );
            }
            continue;
        }

        environment->insert(key, it.value().toString());
    }
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
