// .h include
#include "capabilities/file/fileaccesswrite.h"

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
enum class ContentEncoding
{
    Utf8,
    Base64,
};

const qint64 maxWriteBytes = 20 * 1024 * 1024;

enum class FileWriteOperation
{
    Write,
    Move,
    Delete,
};
QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

QString normalizeToken(const QString &value)
{
    QString normalized = value.trimmed().toLower();
    normalized.remove('-');
    normalized.remove('_');
    normalized.remove(' ');
    return normalized;
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

QString fileWriteOperationName(FileWriteOperation operation)
{
    switch ( operation )
    {
    case FileWriteOperation::Write:
        return QStringLiteral("write");
    case FileWriteOperation::Move:
        return QStringLiteral("move");
    case FileWriteOperation::Delete:
        return QStringLiteral("delete");
    }
    return QStringLiteral("write");
}

bool parseWriteOperation(
    const QJsonObject &paramsObject,
    FileWriteOperation *operation,
    QString *error
)
{
    if ( operation == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: operation output pointer is null");
        }
        return false;
    }

    *operation = FileWriteOperation::Write;
    const QJsonValue value = paramsObject.value(QStringLiteral("operation"));
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }
    if ( !value.isString() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("operation must be string");
        }
        return false;
    }

    const QString normalized = normalizeToken(value.toString());
    if ( normalized.isEmpty() || ( normalized == QStringLiteral("write") ) )
    {
        *operation = FileWriteOperation::Write;
        return true;
    }
    if ( ( normalized == QStringLiteral("move") ) ||
         ( normalized == QStringLiteral("cut") ) )
    {
        *operation = FileWriteOperation::Move;
        return true;
    }
    if ( ( normalized == QStringLiteral("delete") ) ||
         ( normalized == QStringLiteral("remove") ) )
    {
        *operation = FileWriteOperation::Delete;
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("operation must be write, move/cut, or delete/remove");
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

QString encodingName(ContentEncoding encoding)
{
    if ( encoding == ContentEncoding::Base64 )
    {
        return QStringLiteral("base64");
    }
    return QStringLiteral("utf8");
}

bool removePath(
    const QString &absolutePath,
    bool recursive,
    QString *error
)
{
    const QFileInfo targetInfo(absolutePath);
    if ( !targetInfo.exists() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("target does not exist");
        }
        return false;
    }

    if ( targetInfo.isSymLink() || targetInfo.isFile() )
    {
        QFile file(absolutePath);
        if ( !file.remove() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("remove file failed: %1").arg(file.errorString().trimmed());
            }
            return false;
        }
        return true;
    }

    if ( targetInfo.isDir() )
    {
        if ( recursive )
        {
            QDir dir(absolutePath);
            if ( !dir.removeRecursively() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("remove directory recursively failed");
                }
                return false;
            }
            return true;
        }

        QDir parentDir = targetInfo.absoluteDir();
        if ( !parentDir.rmdir(targetInfo.fileName()) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("remove directory failed (directory may be non-empty)");
            }
            return false;
        }
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("unsupported target type");
    }
    return false;
}
}

bool FileWriteAccess::write(
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

    QString parseError;
    bool allowWrite = false;
    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("allowWrite"),
            false,
            &allowWrite,
            &parseError
        ) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }
    if ( !allowWrite )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.write is disabled by default; set allowWrite=true to proceed");
        }
        return false;
    }

    FileWriteOperation operation = FileWriteOperation::Write;
    if ( !parseWriteOperation(paramsObject, &operation, &parseError) )
    {
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    if ( operation == FileWriteOperation::Move )
    {
        const QString destinationPath = [ &paramsObject ]() {
            const QString byDestinationPath = extractString(paramsObject, QStringLiteral("destinationPath"));
            if ( !byDestinationPath.isEmpty() )
            {
                return byDestinationPath;
            }
            return extractString(paramsObject, QStringLiteral("toPath"));
        }();
        if ( destinationPath.isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.write move requires destinationPath or toPath");
            }
            return false;
        }

        const QFileInfo sourceInfo(path);
        if ( !sourceInfo.exists() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.write move source does not exist");
            }
            return false;
        }

        const QFileInfo destinationInfo(destinationPath);
        const QString sourceAbsolutePath = sourceInfo.absoluteFilePath();
        const QString destinationAbsolutePath = destinationInfo.absoluteFilePath();
        if ( sourceAbsolutePath.compare(destinationAbsolutePath, Qt::CaseInsensitive) == 0 )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.write move source and destination must be different");
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

        bool overwrite = false;
        if ( !parseOptionalBool(
                paramsObject,
                QStringLiteral("overwrite"),
                false,
                &overwrite,
                &parseError
            ) )
        {
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        if ( createDirs )
        {
            QDir destinationDir = destinationInfo.absoluteDir();
            if ( !destinationDir.exists() && !destinationDir.mkpath(QStringLiteral(".")) )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.write move failed to create destination parent directories");
                }
                return false;
            }
        }

        const bool destinationExisted = destinationInfo.exists();
        if ( destinationExisted )
        {
            if ( !overwrite )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.write move destination already exists");
                }
                return false;
            }

            QString removeError;
            if ( !removePath(destinationAbsolutePath, true, &removeError) )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.write move failed to remove destination: %1").arg(removeError);
                }
                return false;
            }
        }

        qInfo().noquote() << QStringLiteral(
            "[capability.file.write] move start from=%1 to=%2 overwrite=%3 createDirs=%4"
        ).arg(
            sourceAbsolutePath,
            destinationAbsolutePath,
            overwrite ? QStringLiteral("true") : QStringLiteral("false"),
            createDirs ? QStringLiteral("true") : QStringLiteral("false")
        );

        bool moved = false;
        const bool sourceIsDirectory = sourceInfo.isDir() && !sourceInfo.isSymLink();
        if ( sourceIsDirectory )
        {
            QDir rootDir;
            moved = rootDir.rename(sourceAbsolutePath, destinationAbsolutePath);
        }
        else
        {
            moved = QFile::rename(sourceAbsolutePath, destinationAbsolutePath);
            if ( !moved && sourceInfo.isFile() )
            {
                if ( QFile::copy(sourceAbsolutePath, destinationAbsolutePath) )
                {
                    QFile sourceFile(sourceAbsolutePath);
                    if ( sourceFile.remove() )
                    {
                        moved = true;
                    }
                    else
                    {
                        QFile::remove(destinationAbsolutePath);
                    }
                }
            }
        }

        if ( !moved )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.write move failed");
            }
            return false;
        }

        const QFileInfo movedInfo(destinationAbsolutePath);
        QJsonObject out;
        out.insert(QStringLiteral("operation"), fileWriteOperationName(operation));
        out.insert(QStringLiteral("fromPath"), sourceAbsolutePath);
        out.insert(QStringLiteral("toPath"), movedInfo.absoluteFilePath());
        out.insert(QStringLiteral("path"), movedInfo.absoluteFilePath());
        out.insert(QStringLiteral("targetType"), sourceIsDirectory ? QStringLiteral("directory") : QStringLiteral("file"));
        out.insert(QStringLiteral("overwritten"), destinationExisted);
        out.insert(QStringLiteral("moved"), true);
        *result = out;

        qInfo().noquote() << QStringLiteral(
            "[capability.file.write] move done from=%1 to=%2 targetType=%3 overwritten=%4"
        ).arg(
            sourceAbsolutePath,
            movedInfo.absoluteFilePath(),
            sourceIsDirectory ? QStringLiteral("directory") : QStringLiteral("file"),
            destinationExisted ? QStringLiteral("true") : QStringLiteral("false")
        );
        return true;
    }

    if ( operation == FileWriteOperation::Delete )
    {
        const QFileInfo targetInfo(path);
        if ( !targetInfo.exists() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.write delete target does not exist");
            }
            return false;
        }

        const QString targetAbsolutePath = targetInfo.absoluteFilePath();
        const bool targetIsDirectory = targetInfo.isDir() && !targetInfo.isSymLink();

        qInfo().noquote() << QStringLiteral(
            "[capability.file.write] delete start path=%1 mode=trash"
        ).arg(
            targetAbsolutePath
        );

        QFile targetFile(targetAbsolutePath);
        if ( !targetFile.moveToTrash() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.write delete failed to move target to trash: %1")
                    .arg(targetFile.errorString().trimmed());
            }
            return false;
        }

        QJsonObject out;
        out.insert(QStringLiteral("operation"), fileWriteOperationName(operation));
        out.insert(QStringLiteral("path"), targetAbsolutePath);
        out.insert(QStringLiteral("targetType"), targetIsDirectory ? QStringLiteral("directory") : QStringLiteral("file"));
        out.insert(QStringLiteral("deleted"), true);
        out.insert(QStringLiteral("deleteMode"), QStringLiteral("trash"));
        *result = out;

        qInfo().noquote() << QStringLiteral(
            "[capability.file.write] delete done path=%1 targetType=%2 mode=trash"
        ).arg(
            targetAbsolutePath,
            targetIsDirectory ? QStringLiteral("directory") : QStringLiteral("file")
        );
        return true;
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
    out.insert(QStringLiteral("operation"), fileWriteOperationName(operation));
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

