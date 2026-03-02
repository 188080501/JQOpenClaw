// .h include
#include "capabilities/file/fileaccessread.h"

// Qt lib import
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QtGlobal>

namespace
{
const qint64 defaultReadMaxBytes = 1024 * 1024;
const qint64 maxReadMaxBytes = 20 * 1024 * 1024;
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
    List,
    Rg,
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
    case FileReadOperation::List:
        return QStringLiteral("list");
    case FileReadOperation::Rg:
        return QStringLiteral("rg");
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

    if ( error != nullptr )
    {
        *error = QStringLiteral("operation must be read, list, or rg");
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

        const QDir dir(fileInfo.absoluteFilePath());
        const QFileInfoList entryInfos = dir.entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase
        );

        int directoryCount = 0;
        int fileCount = 0;
        int otherCount = 0;
        bool truncated = false;
        QJsonArray entries;

        for ( const QFileInfo &entryInfo : entryInfos )
        {
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

            if ( includeEntries )
            {
                if ( entries.size() >= maxEntries )
                {
                    truncated = true;
                    continue;
                }

                QJsonObject entry;
                entry.insert(QStringLiteral("name"), entryInfo.fileName());
                entry.insert(QStringLiteral("path"), entryInfo.absoluteFilePath());
                entry.insert(QStringLiteral("type"), entryType);
                entry.insert(QStringLiteral("isSymLink"), entryInfo.isSymLink());
                if ( entryInfo.isFile() )
                {
                    entry.insert(QStringLiteral("sizeBytes"), entryInfo.size());
                }
                entries.append(entry);
            }
        }

        QJsonObject out;
        out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
        out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
        out.insert(QStringLiteral("targetType"), QStringLiteral("directory"));
        out.insert(QStringLiteral("directoryCount"), directoryCount);
        out.insert(QStringLiteral("fileCount"), fileCount);
        out.insert(QStringLiteral("otherCount"), otherCount);
        out.insert(QStringLiteral("totalCount"), entryInfos.size());
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
        if ( invokeTimeoutMs > 0 )
        {
            rgTimeoutMs = qMax(1, qMin(readRgTimeoutMs, invokeTimeoutMs));
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
    out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
    out.insert(QStringLiteral("targetType"), QStringLiteral("file"));
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


