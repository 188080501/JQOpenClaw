// .h include
#include "capabilities/file/fileaccess.h"

// Qt lib import
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QtGlobal>

namespace
{
const qint64 defaultReadMaxBytes = 1024 * 1024;
const qint64 maxReadMaxBytes = 20 * 1024 * 1024;
const qint64 maxWriteBytes = 20 * 1024 * 1024;

enum class ContentEncoding
{
    Utf8,
    Base64,
};

QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

bool parseEncoding(
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
            *error = QStringLiteral("file capability internal error: encoding output pointer is null");
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

    QString normalized = value.toString().trimmed().toLower();
    normalized.remove('-');
    normalized.remove('_');
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

bool parseOptionalBool(
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
            *error = QStringLiteral("file capability internal error: bool output pointer is null");
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

bool parseReadMaxBytes(
    const QJsonObject &paramsObject,
    qint64 *maxBytes,
    QString *error
)
{
    if ( maxBytes == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: maxBytes output pointer is null");
        }
        return false;
    }

    *maxBytes = defaultReadMaxBytes;
    const QJsonValue value = paramsObject.value(QStringLiteral("maxBytes"));
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }
    if ( !value.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("maxBytes must be number");
        }
        return false;
    }

    const double raw = value.toDouble();
    if ( ( raw < 1.0 ) ||
         ( raw > static_cast<double>(maxReadMaxBytes) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "maxBytes must be integer within [1, %1]"
            ).arg(maxReadMaxBytes);
        }
        return false;
    }

    const qint64 parsed = static_cast<qint64>(raw);
    if ( raw != static_cast<double>(parsed) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "maxBytes must be integer within [1, %1]"
            ).arg(maxReadMaxBytes);
        }
        return false;
    }

    *maxBytes = parsed;
    return true;
}

bool decodeContent(
    const QString &content,
    ContentEncoding encoding,
    QByteArray *bytes,
    QString *error
)
{
    if ( bytes == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: content output pointer is null");
        }
        return false;
    }

    if ( encoding == ContentEncoding::Utf8 )
    {
        *bytes = content.toUtf8();
        return true;
    }

    const QByteArray decoded = QByteArray::fromBase64(content.toUtf8());
    if ( content.trimmed().isEmpty() )
    {
        bytes->clear();
        return true;
    }
    if ( decoded.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("content is not valid base64");
        }
        return false;
    }

    *bytes = decoded;
    return true;
}

QString encodeContent(const QByteArray &bytes, ContentEncoding encoding)
{
    if ( encoding == ContentEncoding::Base64 )
    {
        return QString::fromLatin1(bytes.toBase64());
    }
    return QString::fromUtf8(bytes);
}

QString encodingName(ContentEncoding encoding)
{
    if ( encoding == ContentEncoding::Base64 )
    {
        return QStringLiteral("base64");
    }
    return QStringLiteral("utf8");
}
}

bool FileAccess::read(
    const QJsonValue &params,
    QJsonObject *result,
    QString *error
)
{
    if ( result == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read output pointer is null");
        }
        return false;
    }
    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read params must be object");
        }
        return false;
    }

    const QJsonObject paramsObject = params.toObject();
    const QString path = extractString(paramsObject, QStringLiteral("path"));
    if ( path.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read path is required");
        }
        return false;
    }

    ContentEncoding encoding = ContentEncoding::Utf8;
    QString parseError;
    if ( !parseEncoding(
            paramsObject,
            QStringLiteral("encoding"),
            &encoding,
            &parseError
        ) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    qint64 maxBytes = defaultReadMaxBytes;
    if ( !parseReadMaxBytes(paramsObject, &maxBytes, &parseError) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    const QFileInfo fileInfo(path);
    if ( !fileInfo.exists() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read target does not exist");
        }
        return false;
    }
    if ( !fileInfo.isFile() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read target is not a file");
        }
        return false;
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.file.read] start path=%1 maxBytes=%2 encoding=%3"
    ).arg(fileInfo.absoluteFilePath()).arg(maxBytes).arg(encodingName(encoding));

    QFile file(fileInfo.absoluteFilePath());
    if ( !file.open(QIODevice::ReadOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read open failed: %1").arg(file.errorString().trimmed());
        }
        return false;
    }

    QByteArray bytes = file.read(maxBytes + 1);
    if ( bytes.isNull() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read read failed: %1").arg(file.errorString().trimmed());
        }
        return false;
    }

    const bool truncated = bytes.size() > maxBytes;
    if ( truncated )
    {
        bytes.truncate(static_cast<int>(maxBytes));
    }

    QJsonObject out;
    out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
    out.insert(QStringLiteral("encoding"), encodingName(encoding));
    out.insert(QStringLiteral("sizeBytes"), fileInfo.size());
    out.insert(QStringLiteral("readBytes"), bytes.size());
    out.insert(QStringLiteral("truncated"), truncated);
    out.insert(QStringLiteral("content"), encodeContent(bytes, encoding));
    *result = out;

    qInfo().noquote() << QStringLiteral(
        "[capability.file.read] done path=%1 sizeBytes=%2 readBytes=%3 truncated=%4"
    ).arg(fileInfo.absoluteFilePath()).arg(fileInfo.size()).arg(bytes.size()).arg(
        truncated ? QStringLiteral("true") : QStringLiteral("false")
    );
    return true;
}

bool FileAccess::write(
    const QJsonValue &params,
    QJsonObject *result,
    QString *error
)
{
    if ( result == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.write output pointer is null");
        }
        return false;
    }
    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.write params must be object");
        }
        return false;
    }

    const QJsonObject paramsObject = params.toObject();
    const QString path = extractString(paramsObject, QStringLiteral("path"));
    if ( path.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.write path is required");
        }
        return false;
    }

    const QJsonValue contentValue = paramsObject.value(QStringLiteral("content"));
    if ( !contentValue.isString() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.write content must be string");
        }
        return false;
    }

    ContentEncoding encoding = ContentEncoding::Utf8;
    QString parseError;
    if ( !parseEncoding(
            paramsObject,
            QStringLiteral("encoding"),
            &encoding,
            &parseError
        ) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    bool append = false;
    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("append"),
            false,
            &append,
            &parseError
        ) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    bool createDirs = true;
    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("createDirs"),
            true,
            &createDirs,
            &parseError
        ) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    QByteArray contentBytes;
    if ( !decodeContent(contentValue.toString(), encoding, &contentBytes, &parseError) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }
    if ( contentBytes.size() > maxWriteBytes )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "file.write content bytes exceed limit %1"
            ).arg(maxWriteBytes);
        }
        return false;
    }

    const QFileInfo fileInfo(path);
    if ( createDirs )
    {
        QDir dir = fileInfo.absoluteDir();
        if ( !dir.exists() && !dir.mkpath(QStringLiteral(".")) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.write failed to create parent directories");
            }
            return false;
        }
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.file.write] start path=%1 bytes=%2 append=%3 encoding=%4"
    ).arg(
        fileInfo.absoluteFilePath(),
        QString::number(contentBytes.size()),
        append ? QStringLiteral("true") : QStringLiteral("false"),
        encodingName(encoding)
    );

    QFile file(fileInfo.absoluteFilePath());
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    mode |= append ? QIODevice::Append : QIODevice::Truncate;
    if ( !file.open(mode) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.write open failed: %1").arg(file.errorString().trimmed());
        }
        return false;
    }

    const qint64 written = file.write(contentBytes);
    if ( written != contentBytes.size() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.write failed: %1").arg(file.errorString().trimmed());
        }
        return false;
    }
    file.flush();

    const QFileInfo writtenInfo(fileInfo.absoluteFilePath());
    QJsonObject out;
    out.insert(QStringLiteral("path"), writtenInfo.absoluteFilePath());
    out.insert(QStringLiteral("encoding"), encodingName(encoding));
    out.insert(QStringLiteral("appended"), append);
    out.insert(QStringLiteral("bytesWritten"), written);
    out.insert(QStringLiteral("sizeBytes"), writtenInfo.exists() ? writtenInfo.size() : written);
    *result = out;

    qInfo().noquote() << QStringLiteral(
        "[capability.file.write] done path=%1 bytesWritten=%2 sizeBytes=%3"
    ).arg(
        writtenInfo.absoluteFilePath(),
        QString::number(written),
        QString::number(writtenInfo.size())
    );
    return true;
}
