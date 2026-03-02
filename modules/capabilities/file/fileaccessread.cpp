// .h include
#include "capabilities/file/fileaccessread.h"

// Qt lib import
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QtGlobal>
#include <algorithm>
#include <limits>

namespace
{
const qint64 defaultReadMaxBytes = 1024 * 1024;
const qint64 maxReadMaxBytes = 20 * 1024 * 1024;
const qint64 defaultReadChunkBytes = 256 * 1024;
const qint64 maxReadLineSpan = 50000;
const qint64 defaultReadMaxEntries = 200;
const qint64 maxReadMaxEntries = 5000;
const qint64 defaultRgMaxMatches = 200;
const qint64 maxRgMaxMatches = 5000;
const int readRgStartTimeoutMs = 5000;
const int readRgTimeoutMs = 60000;
const int readRgKillWaitTimeoutMs = 3000;

enum class ContentEncoding
{
    Utf8,
    Base64,
};

enum class FileReadOperation
{
    Read,
    Lines,
    List,
    Rg,
    Stat,
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
QString fileReadOperationName(FileReadOperation operation)
{
    switch ( operation )
    {
    case FileReadOperation::Read:
        return QStringLiteral("read");
    case FileReadOperation::Lines:
        return QStringLiteral("lines");
    case FileReadOperation::List:
        return QStringLiteral("list");
    case FileReadOperation::Rg:
        return QStringLiteral("rg");
    case FileReadOperation::Stat:
        return QStringLiteral("stat");
    }
    return QStringLiteral("read");
}

bool parseReadOperation(
    const QJsonObject &paramsObject,
    FileReadOperation *operation,
    QString *error
)
{
    if ( operation == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: read operation output pointer is null");
        }
        return false;
    }

    *operation = FileReadOperation::Read;
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
    if ( normalized.isEmpty() || ( normalized == QStringLiteral("read") ) )
    {
        *operation = FileReadOperation::Read;
        return true;
    }
    if ( ( normalized == QStringLiteral("lines") ) ||
         ( normalized == QStringLiteral("readlines") ) ||
         ( normalized == QStringLiteral("lineread") ) )
    {
        *operation = FileReadOperation::Lines;
        return true;
    }
    if ( normalized == QStringLiteral("list") )
    {
        *operation = FileReadOperation::List;
        return true;
    }
    if ( normalized == QStringLiteral("rg") )
    {
        *operation = FileReadOperation::Rg;
        return true;
    }
    if ( normalized == QStringLiteral("stat") )
    {
        *operation = FileReadOperation::Stat;
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("operation must be read, lines, list, rg, or stat");
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

bool parseIntegerField(
    const QJsonObject &paramsObject,
    const QString &field,
    qint64 defaultValue,
    qint64 minValue,
    qint64 maxValue,
    qint64 *out,
    QString *error
)
{
    if ( out == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: integer output pointer is null");
        }
        return false;
    }

    *out = defaultValue;
    const QJsonValue value = paramsObject.value(field);
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }
    if ( !value.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("%1 must be number").arg(field);
        }
        return false;
    }

    const double raw = value.toDouble();
    if ( ( raw < static_cast<double>(minValue) ) ||
         ( raw > static_cast<double>(maxValue) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "%1 must be integer within [%2, %3]"
            ).arg(field).arg(minValue).arg(maxValue);
        }
        return false;
    }

    const qint64 parsed = static_cast<qint64>(raw);
    if ( raw != static_cast<double>(parsed) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "%1 must be integer within [%2, %3]"
            ).arg(field).arg(minValue).arg(maxValue);
        }
        return false;
    }

    *out = parsed;
    return true;
}

bool parseReadOffsetBytes(
    const QJsonObject &paramsObject,
    qint64 *offsetBytes,
    QString *error
)
{
    if ( offsetBytes == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: offsetBytes output pointer is null");
        }
        return false;
    }

    if ( !parseIntegerField(
            paramsObject,
            QStringLiteral("offsetBytes"),
            0,
            0,
            std::numeric_limits<qint64>::max(),
            offsetBytes,
            error
        ) )
    {
        return false;
    }

    const QJsonValue offsetAlias = paramsObject.value(QStringLiteral("offset"));
    if ( !offsetAlias.isUndefined() && !offsetAlias.isNull() )
    {
        if ( !offsetAlias.isDouble() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("offset must be number");
            }
            return false;
        }

        const double raw = offsetAlias.toDouble();
        if ( ( raw < 0.0 ) ||
             ( raw > static_cast<double>(std::numeric_limits<qint64>::max()) ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "offset must be integer within [0, %1]"
                ).arg(std::numeric_limits<qint64>::max());
            }
            return false;
        }

        const qint64 parsed = static_cast<qint64>(raw);
        if ( raw != static_cast<double>(parsed) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "offset must be integer within [0, %1]"
                ).arg(std::numeric_limits<qint64>::max());
            }
            return false;
        }

        *offsetBytes = parsed;
    }

    return true;
}

bool parseReadLineBoundary(
    const QJsonObject &paramsObject,
    const QString &primaryField,
    const QString &aliasField,
    qint64 *lineValue,
    QString *error
)
{
    if ( lineValue == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: lineValue output pointer is null");
        }
        return false;
    }

    const QJsonValue primaryValue = paramsObject.value(primaryField);
    const QJsonValue aliasValue = paramsObject.value(aliasField);
    const bool hasPrimary = !primaryValue.isUndefined() && !primaryValue.isNull();
    const bool hasAlias = !aliasValue.isUndefined() && !aliasValue.isNull();
    if ( !hasPrimary && !hasAlias )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("%1 is required").arg(primaryField);
        }
        return false;
    }

    const QJsonValue targetValue = hasPrimary ? primaryValue : aliasValue;
    const QString valueField = hasPrimary ? primaryField : aliasField;
    if ( !targetValue.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("%1 must be number").arg(valueField);
        }
        return false;
    }

    const double raw = targetValue.toDouble();
    if ( ( raw < 1.0 ) ||
         ( raw > static_cast<double>(std::numeric_limits<qint64>::max()) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "%1 must be integer within [1, %2]"
            ).arg(valueField).arg(std::numeric_limits<qint64>::max());
        }
        return false;
    }

    const qint64 parsed = static_cast<qint64>(raw);
    if ( raw != static_cast<double>(parsed) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "%1 must be integer within [1, %2]"
            ).arg(valueField).arg(std::numeric_limits<qint64>::max());
        }
        return false;
    }

    *lineValue = parsed;
    return true;
}

bool parseGlobPatterns(
    const QJsonObject &paramsObject,
    QStringList *globPatterns,
    QString *error
)
{
    if ( globPatterns == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: globPatterns output pointer is null");
        }
        return false;
    }

    globPatterns->clear();

    const QJsonValue globValue = paramsObject.value(QStringLiteral("glob"));
    if ( globValue.isString() )
    {
        const QString pattern = globValue.toString().trimmed();
        if ( !pattern.isEmpty() )
        {
            globPatterns->append(pattern);
        }
    }
    else if ( globValue.isArray() )
    {
        const QJsonArray patterns = globValue.toArray();
        for ( const QJsonValue &patternValue : patterns )
        {
            if ( !patternValue.isString() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("glob array item must be string");
                }
                return false;
            }

            const QString pattern = patternValue.toString().trimmed();
            if ( !pattern.isEmpty() )
            {
                globPatterns->append(pattern);
            }
        }
    }
    else if ( !globValue.isUndefined() && !globValue.isNull() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("glob must be string or string array");
        }
        return false;
    }

    const QJsonValue globPatternsValue = paramsObject.value(QStringLiteral("globPatterns"));
    if ( !globPatternsValue.isUndefined() && !globPatternsValue.isNull() )
    {
        if ( !globPatternsValue.isArray() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("globPatterns must be string array");
            }
            return false;
        }

        const QJsonArray patterns = globPatternsValue.toArray();
        for ( const QJsonValue &patternValue : patterns )
        {
            if ( !patternValue.isString() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("globPatterns array item must be string");
                }
                return false;
            }

            const QString pattern = patternValue.toString().trimmed();
            if ( !pattern.isEmpty() )
            {
                globPatterns->append(pattern);
            }
        }
    }

    return true;
}

QString fileTargetType(const QFileInfo &fileInfo)
{
    if ( fileInfo.isDir() )
    {
        return QStringLiteral("directory");
    }
    if ( fileInfo.isFile() )
    {
        return QStringLiteral("file");
    }
    return QStringLiteral("other");
}

QString normalizedRelativePath(const QDir &rootDir, const QFileInfo &entryInfo)
{
    QString relativePath = rootDir.relativeFilePath(entryInfo.absoluteFilePath());
    relativePath.replace('\\', '/');
    return relativePath;
}

bool matchesGlobPatterns(
    const QStringList &globPatterns,
    const QString &relativePath,
    const QString &fileName
)
{
    if ( globPatterns.isEmpty() )
    {
        return true;
    }

    for ( const QString &pattern : globPatterns )
    {
        if ( QDir::match(pattern, relativePath) ||
             QDir::match(pattern, fileName) )
        {
            return true;
        }
    }
    return false;
}

void appendTimestamp(
    QJsonObject *timestamps,
    const QString &key,
    const QDateTime &dateTime
)
{
    if ( timestamps == nullptr )
    {
        return;
    }
    if ( !dateTime.isValid() )
    {
        return;
    }

    QJsonObject item;
    item.insert(QStringLiteral("iso8601"), dateTime.toString(Qt::ISODateWithMs));
    item.insert(QStringLiteral("epochMs"), dateTime.toMSecsSinceEpoch());
    timestamps->insert(key, item);
}

QJsonObject buildPermissionsObject(const QFileInfo &fileInfo)
{
    const QFileDevice::Permissions permissions = fileInfo.permissions();
    QJsonObject permissionObject;
    permissionObject.insert(
        QStringLiteral("ownerRead"),
        static_cast<bool>(permissions & QFileDevice::ReadOwner)
    );
    permissionObject.insert(
        QStringLiteral("ownerWrite"),
        static_cast<bool>(permissions & QFileDevice::WriteOwner)
    );
    permissionObject.insert(
        QStringLiteral("ownerExecute"),
        static_cast<bool>(permissions & QFileDevice::ExeOwner)
    );
    permissionObject.insert(
        QStringLiteral("groupRead"),
        static_cast<bool>(permissions & QFileDevice::ReadGroup)
    );
    permissionObject.insert(
        QStringLiteral("groupWrite"),
        static_cast<bool>(permissions & QFileDevice::WriteGroup)
    );
    permissionObject.insert(
        QStringLiteral("groupExecute"),
        static_cast<bool>(permissions & QFileDevice::ExeGroup)
    );
    permissionObject.insert(
        QStringLiteral("otherRead"),
        static_cast<bool>(permissions & QFileDevice::ReadOther)
    );
    permissionObject.insert(
        QStringLiteral("otherWrite"),
        static_cast<bool>(permissions & QFileDevice::WriteOther)
    );
    permissionObject.insert(
        QStringLiteral("otherExecute"),
        static_cast<bool>(permissions & QFileDevice::ExeOther)
    );
    return permissionObject;
}

QJsonObject buildStatOutput(
    const QFileInfo &fileInfo,
    FileReadOperation operation
)
{
    QJsonObject out;
    out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
    out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
    out.insert(QStringLiteral("targetType"), fileTargetType(fileInfo));
    out.insert(QStringLiteral("name"), fileInfo.fileName());
    out.insert(QStringLiteral("isFile"), fileInfo.isFile());
    out.insert(QStringLiteral("isDir"), fileInfo.isDir());
    out.insert(QStringLiteral("isSymLink"), fileInfo.isSymLink());
    out.insert(QStringLiteral("isHidden"), fileInfo.isHidden());
    out.insert(QStringLiteral("isReadable"), fileInfo.isReadable());
    out.insert(QStringLiteral("isWritable"), fileInfo.isWritable());
    out.insert(QStringLiteral("isExecutable"), fileInfo.isExecutable());
    if ( fileInfo.isFile() )
    {
        out.insert(QStringLiteral("sizeBytes"), fileInfo.size());
    }

    QJsonObject owner;
    owner.insert(QStringLiteral("name"), fileInfo.owner());
    owner.insert(QStringLiteral("id"), static_cast<qint64>(fileInfo.ownerId()));
    owner.insert(QStringLiteral("group"), fileInfo.group());
    owner.insert(QStringLiteral("groupId"), static_cast<qint64>(fileInfo.groupId()));
    out.insert(QStringLiteral("owner"), owner);
    out.insert(QStringLiteral("permissions"), buildPermissionsObject(fileInfo));

    QJsonObject timestamps;
    appendTimestamp(
        &timestamps,
        QStringLiteral("accessTime"),
        fileInfo.fileTime(QFileDevice::FileAccessTime)
    );
    appendTimestamp(
        &timestamps,
        QStringLiteral("birthTime"),
        fileInfo.fileTime(QFileDevice::FileBirthTime)
    );
    appendTimestamp(
        &timestamps,
        QStringLiteral("modificationTime"),
        fileInfo.fileTime(QFileDevice::FileModificationTime)
    );
    appendTimestamp(
        &timestamps,
        QStringLiteral("metadataChangeTime"),
        fileInfo.metadataChangeTime()
    );
    out.insert(QStringLiteral("timestamps"), timestamps);

    return out;
}

QJsonObject buildListEntryObject(
    const QFileInfo &entryInfo,
    const QString &entryType,
    const QString &relativePath
)
{
    QJsonObject entry;
    entry.insert(QStringLiteral("name"), entryInfo.fileName());
    entry.insert(QStringLiteral("path"), entryInfo.absoluteFilePath());
    entry.insert(QStringLiteral("relativePath"), relativePath);
    entry.insert(QStringLiteral("type"), entryType);
    entry.insert(QStringLiteral("isSymLink"), entryInfo.isSymLink());
    if ( entryInfo.isFile() )
    {
        entry.insert(QStringLiteral("sizeBytes"), entryInfo.size());
    }
    return entry;
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

bool parseReadMaxEntries(
    const QJsonObject &paramsObject,
    qint64 *maxEntries,
    QString *error
)
{
    if ( maxEntries == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: maxEntries output pointer is null");
        }
        return false;
    }

    *maxEntries = defaultReadMaxEntries;
    const QJsonValue value = paramsObject.value(QStringLiteral("maxEntries"));
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }
    if ( !value.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("maxEntries must be number");
        }
        return false;
    }

    const double raw = value.toDouble();
    if ( ( raw < 1.0 ) ||
         ( raw > static_cast<double>(maxReadMaxEntries) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "maxEntries must be integer within [1, %1]"
            ).arg(maxReadMaxEntries);
        }
        return false;
    }

    const qint64 parsed = static_cast<qint64>(raw);
    if ( raw != static_cast<double>(parsed) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "maxEntries must be integer within [1, %1]"
            ).arg(maxReadMaxEntries);
        }
        return false;
    }

    *maxEntries = parsed;
    return true;
}

bool parseRgMaxMatches(
    const QJsonObject &paramsObject,
    qint64 *maxMatches,
    QString *error
)
{
    if ( maxMatches == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file capability internal error: maxMatches output pointer is null");
        }
        return false;
    }

    *maxMatches = defaultRgMaxMatches;
    const QJsonValue value = paramsObject.value(QStringLiteral("maxMatches"));
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }
    if ( !value.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("maxMatches must be number");
        }
        return false;
    }

    const double raw = value.toDouble();
    if ( ( raw < 1.0 ) ||
         ( raw > static_cast<double>(maxRgMaxMatches) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "maxMatches must be integer within [1, %1]"
            ).arg(maxRgMaxMatches);
        }
        return false;
    }

    const qint64 parsed = static_cast<qint64>(raw);
    if ( raw != static_cast<double>(parsed) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "maxMatches must be integer within [1, %1]"
            ).arg(maxRgMaxMatches);
        }
        return false;
    }

    *maxMatches = parsed;
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

bool FileReadAccess::read(
    const QJsonValue &params,
    int invokeTimeoutMs,
    QJsonObject *result,
    QString *error,
    bool *invalidParams
)
{
    if ( invalidParams != nullptr )
    {
        *invalidParams = false;
    }

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
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
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
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read path is required");
        }
        return false;
    }

    QString parseError;
    FileReadOperation operation = FileReadOperation::Read;
    if ( !parseReadOperation(paramsObject, &operation, &parseError) )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
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

    if ( operation == FileReadOperation::List )
    {
        if ( !fileInfo.isDir() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read list target is not directory");
            }
            return false;
        }

        bool includeEntries = true;
        if ( !parseOptionalBool(
                paramsObject,
                QStringLiteral("includeEntries"),
                true,
                &includeEntries,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        bool recursive = false;
        if ( !parseOptionalBool(
                paramsObject,
                QStringLiteral("recursive"),
                false,
                &recursive,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        bool includeHidden = true;
        if ( !parseOptionalBool(
                paramsObject,
                QStringLiteral("includeHidden"),
                true,
                &includeHidden,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        QStringList globPatterns;
        if ( !parseGlobPatterns(
                paramsObject,
                &globPatterns,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        qint64 maxEntries = defaultReadMaxEntries;
        if ( includeEntries )
        {
            if ( !parseReadMaxEntries(paramsObject, &maxEntries, &parseError) )
            {
                if ( invalidParams != nullptr )
                {
                    *invalidParams = true;
                }
                if ( error != nullptr )
                {
                    *error = parseError;
                }
                return false;
            }
        }

        const QDir rootDir(fileInfo.absoluteFilePath());
        QDir::Filters entryFilters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if ( includeHidden )
        {
            entryFilters |= QDir::Hidden | QDir::System;
        }

        QFileInfoList entryInfos;
        if ( recursive )
        {
            QDirIterator iterator(
                rootDir.absolutePath(),
                entryFilters,
                QDirIterator::Subdirectories
            );
            while ( iterator.hasNext() )
            {
                iterator.next();
                entryInfos.append(iterator.fileInfo());
            }

            std::sort(
                entryInfos.begin(),
                entryInfos.end(),
                [](const QFileInfo &a, const QFileInfo &b)
                {
                    if ( a.isDir() != b.isDir() )
                    {
                        return a.isDir();
                    }
                    return QString::compare(
                        a.absoluteFilePath(),
                        b.absoluteFilePath(),
                        Qt::CaseInsensitive
                    ) < 0;
                }
            );
        }
        else
        {
            entryInfos = rootDir.entryInfoList(
                entryFilters,
                QDir::DirsFirst | QDir::Name | QDir::IgnoreCase
            );
        }

        int directoryCount = 0;
        int fileCount = 0;
        int otherCount = 0;
        int totalCount = 0;
        bool truncated = false;
        QJsonArray entries;

        for ( const QFileInfo &entryInfo : entryInfos )
        {
            const QString relativePath = normalizedRelativePath(rootDir, entryInfo);
            if ( !matchesGlobPatterns(
                    globPatterns,
                    relativePath,
                    entryInfo.fileName()
                ) )
            {
                continue;
            }

            QString entryType = QStringLiteral("other");
            if ( entryInfo.isDir() )
            {
                ++directoryCount;
                entryType = QStringLiteral("directory");
            }
            else if ( entryInfo.isFile() )
            {
                ++fileCount;
                entryType = QStringLiteral("file");
            }
            else
            {
                ++otherCount;
            }
            ++totalCount;

            if ( includeEntries )
            {
                if ( entries.size() >= maxEntries )
                {
                    truncated = true;
                    continue;
                }

                entries.append(
                    buildListEntryObject(
                        entryInfo,
                        entryType,
                        relativePath
                    )
                );
            }
        }

        QJsonObject out;
        out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
        out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
        out.insert(QStringLiteral("targetType"), QStringLiteral("directory"));
        out.insert(QStringLiteral("recursive"), recursive);
        out.insert(QStringLiteral("includeHidden"), includeHidden);
        if ( !globPatterns.isEmpty() )
        {
            QJsonArray globArray;
            for ( const QString &pattern : globPatterns )
            {
                globArray.append(pattern);
            }
            out.insert(QStringLiteral("glob"), globArray);
        }
        out.insert(QStringLiteral("directoryCount"), directoryCount);
        out.insert(QStringLiteral("fileCount"), fileCount);
        out.insert(QStringLiteral("otherCount"), otherCount);
        out.insert(QStringLiteral("totalCount"), totalCount);
        out.insert(QStringLiteral("includeEntries"), includeEntries);
        if ( includeEntries )
        {
            out.insert(QStringLiteral("maxEntries"), maxEntries);
            out.insert(QStringLiteral("truncated"), truncated);
            out.insert(QStringLiteral("entries"), entries);
        }

        *result = out;
        return true;
    }

    if ( operation == FileReadOperation::Stat )
    {
        *result = buildStatOutput(fileInfo, operation);
        return true;
    }

    if ( operation == FileReadOperation::Lines )
    {
        if ( !fileInfo.isFile() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read lines target is not a file");
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
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }
        if ( encoding != ContentEncoding::Utf8 )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read lines encoding must be utf8");
            }
            return false;
        }

        qint64 startLine = 0;
        if ( !parseReadLineBoundary(
                paramsObject,
                QStringLiteral("startLine"),
                QStringLiteral("fromLine"),
                &startLine,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        qint64 endLine = 0;
        if ( !parseReadLineBoundary(
                paramsObject,
                QStringLiteral("endLine"),
                QStringLiteral("toLine"),
                &endLine,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        if ( endLine < startLine )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = QStringLiteral("endLine must be >= startLine");
            }
            return false;
        }

        const qint64 lineSpan = ( endLine - startLine + 1 );
        if ( lineSpan > maxReadLineSpan )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "line span must be integer within [1, %1]"
                ).arg(maxReadLineSpan);
            }
            return false;
        }

        qInfo().noquote() << QStringLiteral(
            "[capability.file.read] lines start path=%1 startLine=%2 endLine=%3"
        ).arg(
            fileInfo.absoluteFilePath(),
            QString::number(startLine),
            QString::number(endLine)
        );

        QFile file(fileInfo.absoluteFilePath());
        if ( !file.open(QIODevice::ReadOnly) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read open failed: %1").arg(file.errorString().trimmed());
            }
            return false;
        }

        qint64 currentLine = 0;
        QJsonArray lines;
        QStringList lineTexts;
        while ( !file.atEnd() && ( currentLine < endLine ) )
        {
            QByteArray rawLine = file.readLine();
            if ( rawLine.isNull() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.read read failed: %1").arg(file.errorString().trimmed());
                }
                return false;
            }

            if ( rawLine.isEmpty() && file.atEnd() )
            {
                break;
            }

            ++currentLine;
            if ( currentLine < startLine )
            {
                continue;
            }

            while ( rawLine.endsWith('\n') ||
                    rawLine.endsWith('\r') )
            {
                rawLine.chop(1);
            }

            const QString lineText = QString::fromUtf8(rawLine);
            QJsonObject lineItem;
            lineItem.insert(QStringLiteral("lineNumber"), currentLine);
            lineItem.insert(QStringLiteral("text"), lineText);
            lines.append(lineItem);
            lineTexts.append(lineText);
        }

        const bool eof = file.atEnd();
        const bool hasMore = !eof;

        QJsonObject out;
        out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
        out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
        out.insert(QStringLiteral("targetType"), QStringLiteral("file"));
        out.insert(QStringLiteral("encoding"), QStringLiteral("utf8"));
        out.insert(QStringLiteral("startLine"), startLine);
        out.insert(QStringLiteral("endLine"), endLine);
        out.insert(QStringLiteral("returnedLineCount"), lines.size());
        out.insert(QStringLiteral("hasMore"), hasMore);
        out.insert(QStringLiteral("eof"), eof);
        out.insert(QStringLiteral("content"), lineTexts.join(QLatin1Char('\n')));
        out.insert(QStringLiteral("lines"), lines);
        *result = out;

        qInfo().noquote() << QStringLiteral(
            "[capability.file.read] lines done path=%1 startLine=%2 endLine=%3 returnedLineCount=%4 eof=%5"
        ).arg(
            fileInfo.absoluteFilePath(),
            QString::number(startLine),
            QString::number(endLine),
            QString::number(lines.size()),
            eof ? QStringLiteral("true") : QStringLiteral("false")
        );
        return true;
    }

    if ( operation == FileReadOperation::Rg )
    {
        const QString pattern = extractString(paramsObject, QStringLiteral("pattern"));
        if ( pattern.isEmpty() )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read rg pattern is required");
            }
            return false;
        }

        qint64 maxMatches = defaultRgMaxMatches;
        if ( !parseRgMaxMatches(paramsObject, &maxMatches, &parseError) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        bool caseSensitive = false;
        if ( !parseOptionalBool(
                paramsObject,
                QStringLiteral("caseSensitive"),
                false,
                &caseSensitive,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        bool includeHidden = false;
        if ( !parseOptionalBool(
                paramsObject,
                QStringLiteral("includeHidden"),
                false,
                &includeHidden,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        bool literal = false;
        if ( !parseOptionalBool(
                paramsObject,
                QStringLiteral("literal"),
                false,
                &literal,
                &parseError
            ) )
        {
            if ( invalidParams != nullptr )
            {
                *invalidParams = true;
            }
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }

        QStringList rgArguments;
        rgArguments << QStringLiteral("--json")
                    << QStringLiteral("--line-number")
                    << QStringLiteral("--color")
                    << QStringLiteral("never")
                    << QStringLiteral("--max-count")
                    << QString::number(maxMatches);
        if ( !caseSensitive )
        {
            rgArguments << QStringLiteral("--ignore-case");
        }
        if ( includeHidden )
        {
            rgArguments << QStringLiteral("--hidden");
        }
        if ( literal )
        {
            rgArguments << QStringLiteral("--fixed-strings");
        }
        rgArguments << pattern << fileInfo.absoluteFilePath();

        int rgTimeoutMs = readRgTimeoutMs;
        if ( invokeTimeoutMs >= 0 )
        {
            rgTimeoutMs = qMin(readRgTimeoutMs, invokeTimeoutMs);
        }

        qInfo().noquote() << QStringLiteral(
            "[capability.file.read] rg start path=%1 pattern=%2 maxMatches=%3 caseSensitive=%4 includeHidden=%5 literal=%6 timeoutMs=%7"
        ).arg(
            fileInfo.absoluteFilePath(),
            pattern,
            QString::number(maxMatches),
            caseSensitive ? QStringLiteral("true") : QStringLiteral("false"),
            includeHidden ? QStringLiteral("true") : QStringLiteral("false"),
            literal ? QStringLiteral("true") : QStringLiteral("false"),
            QString::number(rgTimeoutMs)
        );

        QProcess process;
        process.start(QStringLiteral("rg"), rgArguments);
        if ( !process.waitForStarted(readRgStartTimeoutMs) )
        {
            if ( error != nullptr )
            {
                const QString startError = process.errorString().trimmed();
                *error = startError.isEmpty()
                    ? QStringLiteral("file.read rg failed to start")
                    : QStringLiteral("file.read rg failed to start: %1").arg(startError);
            }
            return false;
        }

        if ( !process.waitForFinished(rgTimeoutMs) )
        {
            process.kill();
            process.waitForFinished(readRgKillWaitTimeoutMs);
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read rg timed out");
            }
            return false;
        }

        if ( process.exitStatus() != QProcess::NormalExit )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read rg crashed");
            }
            return false;
        }

        const int rgExitCode = process.exitCode();
        const QByteArray stdoutBytes = process.readAllStandardOutput();
        const QByteArray stderrBytes = process.readAllStandardError();
        const QString stderrText = QString::fromLocal8Bit(stderrBytes).trimmed();
        if ( rgExitCode == 2 )
        {
            if ( error != nullptr )
            {
                *error = stderrText.isEmpty()
                    ? QStringLiteral("file.read rg failed")
                    : QStringLiteral("file.read rg failed: %1").arg(stderrText);
            }
            return false;
        }

        QJsonArray matches;
        QSet<QString> matchedFiles;
        bool truncated = false;
        const QList<QByteArray> outputLines = stdoutBytes.split('\n');
        for ( const QByteArray &rawLine : outputLines )
        {
            const QByteArray jsonLine = rawLine.trimmed();
            if ( jsonLine.isEmpty() )
            {
                continue;
            }

            QJsonParseError jsonParseError;
            const QJsonDocument jsonDocument =
                QJsonDocument::fromJson(jsonLine, &jsonParseError);
            if ( jsonParseError.error != QJsonParseError::NoError )
            {
                continue;
            }
            if ( !jsonDocument.isObject() )
            {
                continue;
            }

            const QJsonObject envelope = jsonDocument.object();
            if ( envelope.value(QStringLiteral("type")).toString() != QStringLiteral("match") )
            {
                continue;
            }

            const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
            QString matchPath =
                data.value(QStringLiteral("path")).toObject()
                    .value(QStringLiteral("text")).toString();
            if ( matchPath.trimmed().isEmpty() )
            {
                matchPath = fileInfo.absoluteFilePath();
            }
            matchedFiles.insert(matchPath);

            QString lineText =
                data.value(QStringLiteral("lines")).toObject()
                    .value(QStringLiteral("text")).toString();
            while ( lineText.endsWith(QLatin1Char('\n')) ||
                    lineText.endsWith(QLatin1Char('\r')) )
            {
                lineText.chop(1);
            }
            const int lineNumber = data.value(QStringLiteral("line_number")).toInt();
            const QJsonArray submatches = data.value(QStringLiteral("submatches")).toArray();

            if ( submatches.isEmpty() )
            {
                if ( matches.size() >= maxMatches )
                {
                    truncated = true;
                    continue;
                }

                QJsonObject matchItem;
                matchItem.insert(QStringLiteral("path"), matchPath);
                matchItem.insert(QStringLiteral("lineNumber"), lineNumber);
                matchItem.insert(QStringLiteral("columnStart"), 1);
                matchItem.insert(QStringLiteral("columnEnd"), 1);
                matchItem.insert(QStringLiteral("lineText"), lineText);
                matchItem.insert(QStringLiteral("matchText"), QString());
                matches.append(matchItem);
                continue;
            }

            for ( const QJsonValue &submatchValue : submatches )
            {
                if ( matches.size() >= maxMatches )
                {
                    truncated = true;
                    break;
                }

                const QJsonObject submatch = submatchValue.toObject();
                const int start = submatch.value(QStringLiteral("start")).toInt();
                const int end = submatch.value(QStringLiteral("end")).toInt();
                const QString matchText =
                    submatch.value(QStringLiteral("match")).toObject()
                        .value(QStringLiteral("text")).toString();

                QJsonObject matchItem;
                matchItem.insert(QStringLiteral("path"), matchPath);
                matchItem.insert(QStringLiteral("lineNumber"), lineNumber);
                matchItem.insert(QStringLiteral("columnStart"), start + 1);
                matchItem.insert(QStringLiteral("columnEnd"), end);
                matchItem.insert(QStringLiteral("lineText"), lineText);
                matchItem.insert(QStringLiteral("matchText"), matchText);
                matches.append(matchItem);
            }
        }

        QJsonObject out;
        out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
        out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
        out.insert(
            QStringLiteral("targetType"),
            fileInfo.isDir() ? QStringLiteral("directory") : QStringLiteral("file")
        );
        out.insert(QStringLiteral("pattern"), pattern);
        out.insert(QStringLiteral("caseSensitive"), caseSensitive);
        out.insert(QStringLiteral("includeHidden"), includeHidden);
        out.insert(QStringLiteral("literal"), literal);
        out.insert(QStringLiteral("maxMatches"), maxMatches);
        out.insert(QStringLiteral("matchCount"), matches.size());
        out.insert(QStringLiteral("fileCount"), matchedFiles.size());
        out.insert(QStringLiteral("truncated"), truncated);
        out.insert(QStringLiteral("rgExitCode"), rgExitCode);
        if ( !stderrText.isEmpty() )
        {
            out.insert(QStringLiteral("stderr"), stderrText);
        }
        out.insert(QStringLiteral("matches"), matches);

        *result = out;
        qInfo().noquote() << QStringLiteral(
            "[capability.file.read] rg done path=%1 matches=%2 files=%3 exitCode=%4"
        ).arg(
            fileInfo.absoluteFilePath(),
            QString::number(matches.size()),
            QString::number(matchedFiles.size()),
            QString::number(rgExitCode)
        );
        return true;
    }

    if ( !fileInfo.isFile() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read read target is not a file");
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
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    qint64 maxBytes = defaultReadMaxBytes;
    if ( !parseReadMaxBytes(paramsObject, &maxBytes, &parseError) )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

    qint64 offsetBytes = 0;
    if ( !parseReadOffsetBytes(paramsObject, &offsetBytes, &parseError) )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }
    if ( offsetBytes > fileInfo.size() )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "offsetBytes must be integer within [0, %1]"
            ).arg(fileInfo.size());
        }
        return false;
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.file.read] start path=%1 offsetBytes=%2 maxBytes=%3 encoding=%4"
    ).arg(
        fileInfo.absoluteFilePath(),
        QString::number(offsetBytes),
        QString::number(maxBytes),
        encodingName(encoding)
    );

    QFile file(fileInfo.absoluteFilePath());
    if ( !file.open(QIODevice::ReadOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read open failed: %1").arg(file.errorString().trimmed());
        }
        return false;
    }

    if ( !file.seek(offsetBytes) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("file.read seek failed: %1").arg(file.errorString().trimmed());
        }
        return false;
    }

    const qint64 readBudgetBytes = maxBytes + 1;
    qint64 remainingBytes = readBudgetBytes;
    QByteArray bytes;
    bytes.reserve(static_cast<int>(qMin(readBudgetBytes, defaultReadMaxBytes)));
    while ( remainingBytes > 0 )
    {
        const qint64 currentReadBytes = qMin(remainingBytes, defaultReadChunkBytes);
        const QByteArray chunk = file.read(currentReadBytes);
        if ( chunk.isNull() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read read failed: %1").arg(file.errorString().trimmed());
            }
            return false;
        }
        if ( chunk.isEmpty() )
        {
            break;
        }

        bytes.append(chunk);
        remainingBytes -= chunk.size();
    }

    const bool truncated = bytes.size() > maxBytes;
    if ( truncated )
    {
        bytes.truncate(static_cast<int>(maxBytes));
    }
    const qint64 readBytes = bytes.size();
    const qint64 nextOffsetBytes = offsetBytes + readBytes;
    const bool hasMore = nextOffsetBytes < fileInfo.size();

    QJsonObject out;
    out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
    out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
    out.insert(QStringLiteral("targetType"), QStringLiteral("file"));
    out.insert(QStringLiteral("encoding"), encodingName(encoding));
    out.insert(QStringLiteral("sizeBytes"), fileInfo.size());
    out.insert(QStringLiteral("offsetBytes"), offsetBytes);
    out.insert(QStringLiteral("nextOffsetBytes"), nextOffsetBytes);
    out.insert(QStringLiteral("readBytes"), readBytes);
    out.insert(QStringLiteral("hasMore"), hasMore);
    out.insert(QStringLiteral("eof"), !hasMore);
    out.insert(QStringLiteral("truncated"), truncated);
    out.insert(QStringLiteral("content"), encodeContent(bytes, encoding));
    *result = out;

    qInfo().noquote() << QStringLiteral(
        "[capability.file.read] done path=%1 offsetBytes=%2 sizeBytes=%3 readBytes=%4 nextOffsetBytes=%5 truncated=%6 hasMore=%7"
    ).arg(
        fileInfo.absoluteFilePath(),
        QString::number(offsetBytes),
        QString::number(fileInfo.size()),
        QString::number(readBytes),
        QString::number(nextOffsetBytes),
        truncated ? QStringLiteral("true") : QStringLiteral("false")
    ).arg(
        hasMore ? QStringLiteral("true") : QStringLiteral("false")
    );
    return true;
}


