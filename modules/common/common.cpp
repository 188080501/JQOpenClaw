// .h include
#include "common/common.h"

// Qt lib import
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
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

bool parseStringField(
    const QJsonObject &paramsObject,
    const QString &field,
    bool required,
    bool trim,
    bool allowEmpty,
    bool missingAsTypeError,
    QString *value,
    QString *error,
    const QString &scope
)
{
    if ( value == nullptr )
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

    value->clear();
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        if ( !required )
        {
            return true;
        }
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                missingAsTypeError
                    ? QStringLiteral("%1 must be string").arg(field)
                    : QStringLiteral("requires %1").arg(field)
            );
        }
        return false;
    }
    if ( !rawValue.isString() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be string").arg(field)
            );
        }
        return false;
    }

    QString parsedValue = rawValue.toString();
    if ( trim )
    {
        parsedValue = parsedValue.trimmed();
    }
    if ( !allowEmpty && parsedValue.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must not be empty").arg(field)
            );
        }
        return false;
    }

    *value = parsedValue;
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

QString Common::extractFirstStringTrimmed(const QJsonObject &object, const QStringList &keys)
{
    for ( const QString &key : keys )
    {
        const QString value = extractStringTrimmed(object, key);
        if ( !value.isEmpty() )
        {
            return value;
        }
    }
    return QString();
}

bool Common::parseOptionalTrimmedStringAlias(
    const QJsonObject &paramsObject,
    const QString &primaryField,
    const QString &aliasField,
    QString *value,
    QString *error,
    const QString &scope
)
{
    if ( value == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: %1 output pointer is null").arg(primaryField)
            );
        }
        return false;
    }

    value->clear();
    const QJsonValue primaryValue = paramsObject.value(primaryField);
    if ( !primaryValue.isUndefined() && !primaryValue.isNull() )
    {
        if ( !primaryValue.isString() )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 must be string").arg(primaryField)
                );
            }
            return false;
        }

        const QString parsedPrimaryValue = primaryValue.toString().trimmed();
        if ( !parsedPrimaryValue.isEmpty() )
        {
            *value = parsedPrimaryValue;
            return true;
        }
    }

    const QJsonValue aliasValue = paramsObject.value(aliasField);
    if ( !aliasValue.isUndefined() && !aliasValue.isNull() )
    {
        if ( !aliasValue.isString() )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1 must be string").arg(aliasField)
                );
            }
            return false;
        }
        *value = aliasValue.toString().trimmed();
    }
    return true;
}

bool Common::parseRequiredTrimmedStringAlias(
    const QJsonObject &paramsObject,
    const QString &primaryField,
    const QString &aliasField,
    QString *value,
    QString *error,
    const QString &scope,
    const QString &missingMessage
)
{
    if ( !parseOptionalTrimmedStringAlias(
            paramsObject,
            primaryField,
            aliasField,
            value,
            error,
            scope
        ) )
    {
        return false;
    }

    if ( value->isEmpty() )
    {
        if ( error != nullptr )
        {
            const QString message = missingMessage.trimmed().isEmpty()
                ? QStringLiteral("requires %1 or %2").arg(primaryField, aliasField)
                : missingMessage;
            *error = scopedMessage(scope, message);
        }
        return false;
    }

    return true;
}

void Common::resetInvalidParams(bool *invalidParams)
{
    if ( invalidParams != nullptr )
    {
        *invalidParams = false;
    }
}

void Common::markInvalidParams(bool *invalidParams)
{
    if ( invalidParams != nullptr )
    {
        *invalidParams = true;
    }
}

bool Common::failWithError(QString *error, const QString &message)
{
    if ( error != nullptr )
    {
        *error = message;
    }
    return false;
}

bool Common::failIfNull(const void *pointer, QString *error, const QString &message)
{
    if ( pointer != nullptr )
    {
        return true;
    }
    return failWithError(error, message);
}

bool Common::failInvalidParams(bool *invalidParams, QString *error, const QString &message)
{
    markInvalidParams(invalidParams);
    return failWithError(error, message);
}

bool Common::parseParamsObject(
    const QJsonValue &params,
    QJsonObject *paramsObject,
    QString *error,
    const QString &scope
)
{
    if ( paramsObject == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: params object output pointer is null")
            );
        }
        return false;
    }
    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("params must be object")
            );
        }
        return false;
    }

    *paramsObject = params.toObject();
    return true;
}

bool Common::parseOptionalString(
    const QJsonObject &paramsObject,
    const QString &field,
    QString *value,
    QString *error,
    const QString &scope,
    bool trim
)
{
    return parseStringField(
        paramsObject,
        field,
        false,
        trim,
        true,
        false,
        value,
        error,
        scope
    );
}

bool Common::parseOptionalTrimmedString(
    const QJsonObject &paramsObject,
    const QString &field,
    QString *value,
    QString *error,
    const QString &scope
)
{
    return parseOptionalString(
        paramsObject,
        field,
        value,
        error,
        scope,
        true
    );
}

bool Common::parseRequiredString(
    const QJsonObject &paramsObject,
    const QString &field,
    QString *value,
    QString *error,
    const QString &scope,
    bool trim,
    bool allowEmpty,
    bool missingAsTypeError
)
{
    return parseStringField(
        paramsObject,
        field,
        true,
        trim,
        allowEmpty,
        missingAsTypeError,
        value,
        error,
        scope
    );
}

bool Common::parseRequiredTrimmedString(
    const QJsonObject &paramsObject,
    const QString &field,
    QString *value,
    QString *error,
    const QString &scope,
    bool missingAsTypeError
)
{
    return parseRequiredString(
        paramsObject,
        field,
        value,
        error,
        scope,
        true,
        false,
        missingAsTypeError
    );
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

bool Common::parseOptionalTrimmedStringArray(
    const QJsonObject &paramsObject,
    const QString &field,
    QStringList *out,
    QString *error,
    const QString &scope,
    bool skipEmpty
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

    QStringList rawValues;
    if ( !parseOptionalStringArray(paramsObject, field, &rawValues, error, scope) )
    {
        return false;
    }

    out->clear();
    for ( int index = 0; index < rawValues.size(); ++index )
    {
        const QString parsedValue = rawValues.at(index).trimmed();
        if ( parsedValue.isEmpty() )
        {
            if ( skipEmpty )
            {
                continue;
            }
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1[%2] must not be empty").arg(field).arg(index)
                );
            }
            return false;
        }
        out->append(parsedValue);
    }
    return true;
}

bool Common::parseOptionalStringOrStringArray(
    const QJsonObject &paramsObject,
    const QString &field,
    QStringList *out,
    QString *error,
    const QString &scope,
    bool trim,
    bool skipEmpty
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
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        return true;
    }

    const auto appendString =
        [ field, error, scope, trim, skipEmpty, out ](const QString &sourceValue, int index) -> bool
        {
            QString parsedValue = sourceValue;
            if ( trim )
            {
                parsedValue = parsedValue.trimmed();
            }
            if ( parsedValue.isEmpty() )
            {
                if ( skipEmpty )
                {
                    return true;
                }
                if ( error != nullptr )
                {
                    *error = scopedMessage(
                        scope,
                        ( index < 0 )
                            ? QStringLiteral("%1 must not be empty").arg(field)
                            : QStringLiteral("%1[%2] must not be empty").arg(field).arg(index)
                    );
                }
                return false;
            }
            out->append(parsedValue);
            return true;
        };

    if ( rawValue.isString() )
    {
        return appendString(rawValue.toString(), -1);
    }

    if ( !rawValue.isArray() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be string or string array").arg(field)
            );
        }
        return false;
    }

    const QJsonArray rawArray = rawValue.toArray();
    for ( int index = 0; index < rawArray.size(); ++index )
    {
        const QJsonValue itemValue = rawArray.at(index);
        if ( !itemValue.isString() )
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
        if ( !appendString(itemValue.toString(), index) )
        {
            return false;
        }
    }
    return true;
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

bool Common::parseRequiredObjectArray(
    const QJsonObject &paramsObject,
    const QString &field,
    int minCount,
    int maxCount,
    QJsonArray *out,
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

    *out = QJsonArray();
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
    if ( !rawValue.isArray() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be array").arg(field)
            );
        }
        return false;
    }

    const QJsonArray arrayValue = rawValue.toArray();
    if ( ( arrayValue.size() < minCount ) || ( arrayValue.size() > maxCount ) )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 count out of range [%2, %3]")
                    .arg(field)
                    .arg(minCount)
                    .arg(maxCount)
            );
        }
        return false;
    }

    for ( int index = 0; index < arrayValue.size(); ++index )
    {
        if ( !arrayValue.at(index).isObject() )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1[%2] must be object").arg(field).arg(index)
                );
            }
            return false;
        }
    }

    *out = arrayValue;
    return true;
}

bool Common::parseJsonObject(
    const QByteArray &jsonBytes,
    QJsonObject *object,
    QString *error
)
{
    if ( object == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("internal error: json object output pointer is null");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(jsonBytes, &parseError);
    if ( parseError.error != QJsonParseError::NoError )
    {
        if ( error != nullptr )
        {
            *error = parseError.errorString();
        }
        return false;
    }
    if ( !json.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("json root must be object");
        }
        return false;
    }

    *object = json.object();
    return true;
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
    return parseOptionalInt(
        paramsObject,
        field,
        minValue,
        maxValue,
        defaultValue,
        timeoutMs,
        error,
        IntegerParseStyle::Number,
        scope
    );
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

    const QString expectedType = ( style == IntegerParseStyle::Integer )
        ? QStringLiteral("integer")
        : QStringLiteral("number");
    const QString expectedWithinRange = QStringLiteral("%1 must be %2 within [%3, %4]")
        .arg(
            field,
            expectedType,
            QString::number(minValue),
            QString::number(maxValue)
        );

    if ( !rawValue.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be %2").arg(field, expectedType)
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
                *error = scopedMessage(scope, expectedWithinRange);
            }
            return false;
        }

        if ( ( doubleValue < static_cast<double>((std::numeric_limits<int>::min)()) ) ||
             ( doubleValue > static_cast<double>((std::numeric_limits<int>::max)()) ) )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(scope, expectedWithinRange);
            }
            return false;
        }

        parsedValue = static_cast<int>(doubleValue);
        if ( doubleValue != static_cast<double>(parsedValue) )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(scope, expectedWithinRange);
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
                *error = scopedMessage(scope, expectedWithinRange);
            }
            return false;
        }
    }

    if ( ( parsedValue < minValue ) ||
         ( parsedValue > maxValue ) )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(scope, expectedWithinRange);
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

bool Common::parseOptionalIntWithPresence(
    const QJsonObject &paramsObject,
    const QString &field,
    int minValue,
    int maxValue,
    int defaultValue,
    int *value,
    bool *hasValue,
    QString *error,
    IntegerParseStyle style,
    const QString &scope
)
{
    if ( hasValue == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: %1 presence output pointer is null").arg(field)
            );
        }
        return false;
    }

    *hasValue = false;
    if ( !parseOptionalInt(
            paramsObject,
            field,
            minValue,
            maxValue,
            defaultValue,
            value,
            error,
            style,
            scope
        ) )
    {
        return false;
    }

    const QJsonValue rawValue = paramsObject.value(field);
    *hasValue = !rawValue.isUndefined() && !rawValue.isNull();
    return true;
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

bool Common::parseInt64Value(
    const QJsonValue &rawValue,
    const QString &field,
    qint64 minValue,
    qint64 maxValue,
    qint64 *value,
    QString *error,
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

    const QString expectedWithinRange = QStringLiteral("%1 must be integer within [%2, %3]")
        .arg(field)
        .arg(minValue)
        .arg(maxValue);

    if ( !rawValue.isDouble() )
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

    const double doubleValue = rawValue.toDouble(std::numeric_limits<double>::quiet_NaN());
    if ( !std::isfinite(doubleValue) )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(scope, expectedWithinRange);
        }
        return false;
    }

    if ( ( doubleValue < static_cast<double>(minValue) ) ||
         ( doubleValue > static_cast<double>(maxValue) ) )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(scope, expectedWithinRange);
        }
        return false;
    }

    const qint64 parsedValue = static_cast<qint64>(doubleValue);
    if ( doubleValue != static_cast<double>(parsedValue) )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(scope, expectedWithinRange);
        }
        return false;
    }

    *value = parsedValue;
    return true;
}

bool Common::parseOptionalInt64(
    const QJsonObject &paramsObject,
    const QString &field,
    qint64 minValue,
    qint64 maxValue,
    qint64 defaultValue,
    qint64 *value,
    QString *error,
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

    return parseInt64Value(rawValue, field, minValue, maxValue, value, error, scope);
}

bool Common::parseRequiredInt64(
    const QJsonObject &paramsObject,
    const QString &field,
    qint64 minValue,
    qint64 maxValue,
    qint64 *value,
    QString *error,
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

    return parseInt64Value(rawValue, field, minValue, maxValue, value, error, scope);
}

bool Common::parseOptionalInt64Alias(
    const QJsonObject &paramsObject,
    const QString &primaryField,
    const QString &aliasField,
    qint64 minValue,
    qint64 maxValue,
    qint64 defaultValue,
    qint64 *value,
    QString *error,
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
    const QJsonValue primaryValue = paramsObject.value(primaryField);
    if ( !primaryValue.isUndefined() && !primaryValue.isNull() )
    {
        return parseInt64Value(primaryValue, primaryField, minValue, maxValue, value, error, scope);
    }

    const QJsonValue aliasValue = paramsObject.value(aliasField);
    if ( !aliasValue.isUndefined() && !aliasValue.isNull() )
    {
        return parseInt64Value(aliasValue, aliasField, minValue, maxValue, value, error, scope);
    }

    return true;
}

bool Common::parseRequiredInt64Alias(
    const QJsonObject &paramsObject,
    const QString &primaryField,
    const QString &aliasField,
    qint64 minValue,
    qint64 maxValue,
    qint64 *value,
    QString *error,
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

    const QJsonValue primaryValue = paramsObject.value(primaryField);
    if ( !primaryValue.isUndefined() && !primaryValue.isNull() )
    {
        return parseInt64Value(primaryValue, primaryField, minValue, maxValue, value, error, scope);
    }

    const QJsonValue aliasValue = paramsObject.value(aliasField);
    if ( !aliasValue.isUndefined() && !aliasValue.isNull() )
    {
        return parseInt64Value(aliasValue, aliasField, minValue, maxValue, value, error, scope);
    }

    if ( error != nullptr )
    {
        *error = scopedMessage(
            scope,
            QStringLiteral("requires %1 or %2").arg(primaryField, aliasField)
        );
    }
    return false;
}

bool Common::parseOptionalToken(
    const QJsonObject &paramsObject,
    const QString &field,
    const QString &defaultValue,
    QString *token,
    QString *error,
    const QString &scope,
    bool normalize
)
{
    if ( token == nullptr )
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

    *token = defaultValue;
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        return true;
    }
    if ( !rawValue.isString() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be string").arg(field)
            );
        }
        return false;
    }

    QString parsedValue;
    if ( normalize )
    {
        parsedValue = normalizeToken(rawValue.toString());
    }
    else
    {
        parsedValue = rawValue.toString().trimmed().toLower();
    }
    if ( parsedValue.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must not be empty").arg(field)
            );
        }
        return false;
    }

    *token = parsedValue;
    return true;
}

bool Common::parseEncoding(
    const QJsonObject &paramsObject,
    const QString &field,
    ContentEncoding *encoding,
    QString *error,
    const QString &scope
)
{
    if ( encoding == nullptr )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("internal error: encoding output pointer is null")
            );
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
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be string").arg(field)
            );
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
        *error = scopedMessage(
            scope,
            QStringLiteral("%1 must be utf8 or base64").arg(field)
        );
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
    const auto insertEnvironmentEntry =
        [environment,
         ignorePathOverride,
         ignoredUpperKeys,
         warningPrefix,
         error,
         scope](const QString &rawKey, const QString &rawValue, const QString &emptyKeyMessage) -> bool
        {
            const QString key = rawKey.trimmed();
            if ( key.isEmpty() )
            {
                if ( error != nullptr )
                {
                    *error = scopedMessage(
                        scope,
                        emptyKeyMessage
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
                    qDebug().noquote() << QStringLiteral(
                        "%1 skip environment key=%2 reason=path_override"
                    ).arg(
                        warningPrefix,
                        key
                    );
                }
                return true;
            }
            if ( ( ignoredUpperKeys != nullptr ) &&
                 ignoredUpperKeys->contains(normalizedKey) )
            {
                if ( !warningPrefix.isEmpty() )
                {
                    qDebug().noquote() << QStringLiteral(
                        "%1 skip environment key=%2 reason=unsafe_key"
                    ).arg(
                        warningPrefix,
                        key
                    );
                }
                return true;
            }

            environment->insert(key, rawValue);
            return true;
        };

    if ( environmentValue.isObject() )
    {
        const QJsonObject environmentObject = environmentValue.toObject();
        for ( auto it = environmentObject.constBegin(); it != environmentObject.constEnd(); ++it )
        {
            if ( !it.value().isString() )
            {
                if ( error != nullptr )
                {
                    *error = scopedMessage(
                        scope,
                        QStringLiteral("%1 key \"%2\" must be string value")
                            .arg(environmentField, it.key().trimmed())
                    );
                }
                return false;
            }

            if ( !insertEnvironmentEntry(
                    it.key(),
                    it.value().toString(),
                    QStringLiteral("%1 contains empty key").arg(environmentField)
                ) )
            {
                return false;
            }
        }
        return true;
    }

    if ( !environmentValue.isArray() )
    {
        if ( error != nullptr )
        {
            *error = scopedMessage(
                scope,
                QStringLiteral("%1 must be object or string array").arg(environmentField)
            );
        }
        return false;
    }

    const QJsonArray environmentArray = environmentValue.toArray();
    for ( int index = 0; index < environmentArray.size(); ++index )
    {
        const QJsonValue itemValue = environmentArray.at(index);
        if ( !itemValue.isString() )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1[%2] must be KEY=VALUE string")
                        .arg(environmentField)
                        .arg(index)
                );
            }
            return false;
        }

        const QString itemText = itemValue.toString();
        const int equalIndex = itemText.indexOf('=');
        if ( equalIndex <= 0 )
        {
            if ( error != nullptr )
            {
                *error = scopedMessage(
                    scope,
                    QStringLiteral("%1[%2] must be KEY=VALUE string")
                        .arg(environmentField)
                        .arg(index)
                );
            }
            return false;
        }

        const QString key = itemText.left(equalIndex);
        const QString value = itemText.mid(equalIndex + 1);
        if ( !insertEnvironmentEntry(
                key,
                value,
                QStringLiteral("%1[%2] has empty key").arg(environmentField).arg(index)
            ) )
        {
            return false;
        }
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
