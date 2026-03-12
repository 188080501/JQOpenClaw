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
#include <QProcessEnvironment>
#include <QSet>
#include <QtGlobal>
#include <algorithm>
#include <limits>

// JQOpenClaw import
#include "common/common.h"

namespace
{
const qint64 defaultReadMaxBytes = 1024 * 1024;
const qint64 maxReadMaxBytes = 2 * 1024 * 1024;
const qint64 defaultReadChunkBytes = 256 * 1024;
const qint64 maxReadLineSpan = 50000;
const qint64 defaultReadMaxEntries = 200;
const qint64 maxReadMaxEntries = 5000;
const qint64 defaultRgMaxMatches = 200;
const qint64 maxRgMaxMatches = 5000;
const int readRgStartTimeoutMs = 5000;
const int readRgTimeoutMs = 60000;
const int readRgKillWaitTimeoutMs = 3000;

QDir::SortFlags entryNameSortFlags()
{
    QDir::SortFlags sortFlags = QDir::DirsFirst | QDir::Name;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    sortFlags |= QDir::IgnoreCase;
#endif
    return sortFlags;
}

const char *readRgPowerShellFallbackScript = R"PS(
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ErrorActionPreference = 'Stop'

$targetPath = $env:JQ_FILE_READ_PATH
$pattern = $env:JQ_FILE_READ_PATTERN
$maxMatches = [int]$env:JQ_FILE_READ_MAX_MATCHES
$caseSensitive = $env:JQ_FILE_READ_CASE_SENSITIVE -eq '1'
$includeHidden = $env:JQ_FILE_READ_INCLUDE_HIDDEN -eq '1'
$literal = $env:JQ_FILE_READ_LITERAL -eq '1'

$files = @()
if (Test-Path -LiteralPath $targetPath -PathType Container)
{
    if ($includeHidden)
    {
        $files = Get-ChildItem -LiteralPath $targetPath -Recurse -File -Force -ErrorAction Stop
    }
    else
    {
        $files = Get-ChildItem -LiteralPath $targetPath -Recurse -File -ErrorAction Stop
    }
}
elseif (Test-Path -LiteralPath $targetPath -PathType Leaf)
{
    $files = @(Get-Item -LiteralPath $targetPath -ErrorAction Stop)
}
else
{
    throw 'target path does not exist'
}

$matches = New-Object System.Collections.ArrayList
$truncated = $false

$selectArgs = @{
    Pattern = $pattern
    ErrorAction = 'Stop'
    AllMatches = $true
}
if ($caseSensitive)
{
    $selectArgs.CaseSensitive = $true
}
if ($literal)
{
    $selectArgs.SimpleMatch = $true
}

foreach ($file in $files)
{
    $lineMatches = Select-String -LiteralPath $file.FullName @selectArgs
    foreach ($lineMatch in $lineMatches)
    {
        $lineText = if ($null -eq $lineMatch.Line) { '' } else { [string]$lineMatch.Line }

        if ($literal)
        {
            if ($caseSensitive)
            {
                $comparison = [System.StringComparison]::Ordinal
            }
            else
            {
                $comparison = [System.StringComparison]::OrdinalIgnoreCase
            }

            $searchOffset = 0
            while ($true)
            {
                if ($matches.Count -ge $maxMatches)
                {
                    $truncated = $true
                    break
                }

                $matchIndex = $lineText.IndexOf($pattern, $searchOffset, $comparison)
                if ($matchIndex -lt 0)
                {
                    break
                }

                $matchLength = [Math]::Max($pattern.Length, 1)
                $safeLength = [Math]::Min($matchLength, [Math]::Max($lineText.Length - $matchIndex, 0))
                $matchedText = if ($safeLength -gt 0) { $lineText.Substring($matchIndex, $safeLength) } else { '' }
                $columnStart = [int]$matchIndex + 1
                $columnEnd = [int]$matchIndex + [int]$matchLength
                [void]$matches.Add(
                    [pscustomobject]@{
                        path = $lineMatch.Path
                        lineNumber = $lineMatch.LineNumber
                        columnStart = $columnStart
                        columnEnd = $columnEnd
                        lineText = $lineText
                        matchText = $matchedText
                    }
                )

                $searchOffset = $matchIndex + $matchLength
                if ($searchOffset -ge $lineText.Length)
                {
                    break
                }
            }

            if ($truncated)
            {
                break
            }

            continue
        }

        if ($null -eq $lineMatch.Matches -or $lineMatch.Matches.Count -eq 0)
        {
            if ($matches.Count -ge $maxMatches)
            {
                $truncated = $true
                break
            }

            [void]$matches.Add(
                [pscustomobject]@{
                    path = $lineMatch.Path
                    lineNumber = $lineMatch.LineNumber
                    columnStart = 1
                    columnEnd = 1
                    lineText = $lineText
                    matchText = ''
                }
            )
            continue
        }

        foreach ($subMatch in $lineMatch.Matches)
        {
            if ($matches.Count -ge $maxMatches)
            {
                $truncated = $true
                break
            }

            $columnStart = [int]$subMatch.Index + 1
            $columnEnd = [int]$subMatch.Index + [int]$subMatch.Length
            [void]$matches.Add(
                [pscustomobject]@{
                    path = $lineMatch.Path
                    lineNumber = $lineMatch.LineNumber
                    columnStart = $columnStart
                    columnEnd = $columnEnd
                    lineText = $lineText
                    matchText = $subMatch.Value
                }
            )
        }

        if ($truncated)
        {
            break
        }
    }

    if ($truncated)
    {
        break
    }
}

[pscustomobject]@{
    matches = $matches
    truncated = $truncated
} | ConvertTo-Json -Compress -Depth 8
)PS";

enum class FileReadOperation
{
    Read,
    Lines,
    List,
    Rg,
    Stat,
    Md5,
};

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
    case FileReadOperation::Md5:
        return QStringLiteral("md5");
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
    QString normalized;
    if ( !Common::parseOptionalToken(
            paramsObject,
            QStringLiteral("operation"),
            QStringLiteral("read"),
            &normalized,
            error
        ) )
    {
        return false;
    }
    if ( normalized == QStringLiteral("read") )
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
    if ( normalized == QStringLiteral("md5") )
    {
        *operation = FileReadOperation::Md5;
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("operation must be read, lines, list, rg, stat, or md5");
    }
    return false;
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

    if ( !Common::parseOptionalInt64Alias(
            paramsObject,
            QStringLiteral("offsetBytes"),
            QStringLiteral("offset"),
            0,
            std::numeric_limits<qint64>::max(),
            0,
            offsetBytes,
            error,
            QString()
        ) )
    {
        return false;
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

    return Common::parseRequiredInt64Alias(
        paramsObject,
        primaryField,
        aliasField,
        1,
        std::numeric_limits<qint64>::max(),
        lineValue,
        error,
        QString()
    );
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
    if ( globValue.isArray() )
    {
        const QJsonArray rawPatterns = globValue.toArray();
        for ( const QJsonValue &rawPattern : rawPatterns )
        {
            if ( !rawPattern.isString() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("glob array item must be string");
                }
                return false;
            }
        }
    }

    QStringList parsedGlob;
    if ( !Common::parseOptionalStringOrStringArray(
            paramsObject,
            QStringLiteral("glob"),
            &parsedGlob,
            error,
            QString(),
            true,
            true
        ) )
    {
        return false;
    }
    globPatterns->append(parsedGlob);

    const QJsonValue globPatternsValue = paramsObject.value(QStringLiteral("globPatterns"));
    if ( globPatternsValue.isArray() )
    {
        const QJsonArray rawPatterns = globPatternsValue.toArray();
        for ( const QJsonValue &rawPattern : rawPatterns )
        {
            if ( !rawPattern.isString() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("globPatterns array item must be string");
                }
                return false;
            }
        }
    }

    QStringList parsedGlobPatterns;
    if ( !Common::parseOptionalTrimmedStringArray(
            paramsObject,
            QStringLiteral("globPatterns"),
            &parsedGlobPatterns,
            error,
            QString(),
            true
        ) )
    {
        return false;
    }
    globPatterns->append(parsedGlobPatterns);

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

    return Common::parseOptionalInt64(
        paramsObject,
        QStringLiteral("maxBytes"),
        1,
        maxReadMaxBytes,
        defaultReadMaxBytes,
        maxBytes,
        error,
        QString()
    );
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

    return Common::parseOptionalInt64(
        paramsObject,
        QStringLiteral("maxEntries"),
        1,
        maxReadMaxEntries,
        defaultReadMaxEntries,
        maxEntries,
        error,
        QString()
    );
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

    return Common::parseOptionalInt64(
        paramsObject,
        QStringLiteral("maxMatches"),
        1,
        maxRgMaxMatches,
        defaultRgMaxMatches,
        maxMatches,
        error,
        QString()
    );
}
QString encodeContent(const QByteArray &bytes, Common::ContentEncoding encoding)
{
    if ( encoding == Common::ContentEncoding::Base64 )
    {
        return QString::fromLatin1(bytes.toBase64());
    }
    return QString::fromUtf8(bytes);
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
    Common::resetInvalidParams(invalidParams);
    if ( !Common::failIfNull(result, error, QStringLiteral("file.read output pointer is null")) )
    {
        return false;
    }
    QString parseError;
    QJsonObject paramsObject;
    if ( !Common::parseParamsObject(
            params,
            &paramsObject,
            &parseError,
            QStringLiteral("file.read")
        ) )
    {
        return Common::failInvalidParams(invalidParams, error, parseError);
    }
    const QString path = Common::extractStringTrimmed(paramsObject, QStringLiteral("path"));
    if ( path.isEmpty() )
    {
        return Common::failInvalidParams(
            invalidParams,
            error,
            QStringLiteral("file.read path is required")
        );
    }

    FileReadOperation operation = FileReadOperation::Read;
    if ( !parseReadOperation(paramsObject, &operation, &parseError) )
    {
        return Common::failInvalidParams(invalidParams, error, parseError);
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
        if ( !Common::parseOptionalBool(
                paramsObject,
                QStringLiteral("includeEntries"),
                true,
                &includeEntries,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        bool recursive = false;
        if ( !Common::parseOptionalBool(
                paramsObject,
                QStringLiteral("recursive"),
                false,
                &recursive,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        bool includeHidden = true;
        if ( !Common::parseOptionalBool(
                paramsObject,
                QStringLiteral("includeHidden"),
                true,
                &includeHidden,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        QStringList globPatterns;
        if ( !parseGlobPatterns(
                paramsObject,
                &globPatterns,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        qint64 maxEntries = defaultReadMaxEntries;
        if ( includeEntries )
        {
            if ( !parseReadMaxEntries(paramsObject, &maxEntries, &parseError) )
            {
                return Common::failInvalidParams(invalidParams, error, parseError);
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
                        Common::pathCaseSensitivity()
                    ) < 0;
                }
            );
        }
        else
        {
            entryInfos = rootDir.entryInfoList(
                entryFilters,
                entryNameSortFlags()
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

    if ( operation == FileReadOperation::Md5 )
    {
        if ( !fileInfo.isFile() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("file.read md5 target is not a file");
            }
            return false;
        }

        QString md5Hex;
        QString md5Error;
        if ( !Common::calculateFileMd5Hex(
                fileInfo.absoluteFilePath(),
                &md5Hex,
                &md5Error,
                QStringLiteral("file.read")
            ) )
        {
            if ( error != nullptr )
            {
                *error = md5Error.isEmpty()
                    ? QStringLiteral("file.read md5 failed")
                    : md5Error;
            }
            return false;
        }

        QJsonObject out;
        out.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
        out.insert(QStringLiteral("operation"), fileReadOperationName(operation));
        out.insert(QStringLiteral("targetType"), QStringLiteral("file"));
        out.insert(QStringLiteral("algorithm"), QStringLiteral("md5"));
        out.insert(QStringLiteral("sizeBytes"), fileInfo.size());
        out.insert(QStringLiteral("md5"), md5Hex);
        *result = out;
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

        Common::ContentEncoding encoding = Common::ContentEncoding::Utf8;
        if ( !Common::parseEncoding(
                paramsObject,
                QStringLiteral("encoding"),
                &encoding,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }
        if ( encoding != Common::ContentEncoding::Utf8 )
        {
            return Common::failInvalidParams(
                invalidParams,
                error,
                QStringLiteral("file.read lines encoding must be utf8")
            );
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
            return Common::failInvalidParams(invalidParams, error, parseError);
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
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        if ( endLine < startLine )
        {
            return Common::failInvalidParams(
                invalidParams,
                error,
                QStringLiteral("endLine must be >= startLine")
            );
        }

        const qint64 lineSpan = ( endLine - startLine + 1 );
        if ( lineSpan > maxReadLineSpan )
        {
            return Common::failInvalidParams(
                invalidParams,
                error,
                QStringLiteral("line span must be integer within [1, %1]").arg(maxReadLineSpan)
            );
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
        const QString pattern = Common::extractStringTrimmed(paramsObject, QStringLiteral("pattern"));
        if ( pattern.isEmpty() )
        {
            return Common::failInvalidParams(
                invalidParams,
                error,
                QStringLiteral("file.read rg pattern is required")
            );
        }

        qint64 maxMatches = defaultRgMaxMatches;
        if ( !parseRgMaxMatches(paramsObject, &maxMatches, &parseError) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        bool caseSensitive = false;
        if ( !Common::parseOptionalBool(
                paramsObject,
                QStringLiteral("caseSensitive"),
                false,
                &caseSensitive,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        bool includeHidden = false;
        if ( !Common::parseOptionalBool(
                paramsObject,
                QStringLiteral("includeHidden"),
                false,
                &includeHidden,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
        }

        bool literal = false;
        if ( !Common::parseOptionalBool(
                paramsObject,
                QStringLiteral("literal"),
                false,
                &literal,
                &parseError
            ) )
        {
            return Common::failInvalidParams(invalidParams, error, parseError);
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

        QString searchBackend = QStringLiteral("rg");
        int searchExitCode = 0;
        QByteArray stdoutBytes;
        QByteArray stderrBytes;
        bool usePowerShellFallback = false;

        QProcess rgProcess;
        rgProcess.start(QStringLiteral("rg"), rgArguments);
        if ( !rgProcess.waitForStarted(readRgStartTimeoutMs) )
        {
            usePowerShellFallback = true;
            const QString rgStartError = rgProcess.errorString().trimmed();
            const QString rgStartErrorLower = rgStartError.toLower();
            const bool rgNotFound =
                ( rgProcess.error() == QProcess::FailedToStart ) &&
                (
                    rgStartError.contains(QStringLiteral("系统找不到指定的文件")) ||
                    rgStartErrorLower.contains(QStringLiteral("not found")) ||
                    rgStartErrorLower.contains(QStringLiteral("no such file"))
                );
            if ( rgNotFound )
            {
                qInfo().noquote() << QStringLiteral(
                    "[capability.file.read] rg not found, using powershell select-string fallback"
                );
            }
            else
            {
                qWarning().noquote() << QStringLiteral(
                    "[capability.file.read] rg failed to start, fallback to powershell select-string: %1"
                ).arg(rgStartError);
            }
        }
        else
        {
            if ( !rgProcess.waitForFinished(rgTimeoutMs) )
            {
                rgProcess.kill();
                rgProcess.waitForFinished(readRgKillWaitTimeoutMs);
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.read rg timed out");
                }
                return false;
            }

            if ( rgProcess.exitStatus() != QProcess::NormalExit )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.read rg crashed");
                }
                return false;
            }

            searchExitCode = rgProcess.exitCode();
            stdoutBytes = rgProcess.readAllStandardOutput();
            stderrBytes = rgProcess.readAllStandardError();
            const QString rgStderrText = QString::fromLocal8Bit(stderrBytes).trimmed();
            if ( searchExitCode == 2 )
            {
                if ( error != nullptr )
                {
                    *error = rgStderrText.isEmpty()
                        ? QStringLiteral("file.read rg failed")
                        : QStringLiteral("file.read rg failed: %1").arg(rgStderrText);
                }
                return false;
            }
        }

        if ( usePowerShellFallback )
        {
            searchBackend = QStringLiteral("powershell.select-string");
            QProcessEnvironment processEnvironment = QProcessEnvironment::systemEnvironment();
            processEnvironment.insert(QStringLiteral("JQ_FILE_READ_PATH"), fileInfo.absoluteFilePath());
            processEnvironment.insert(QStringLiteral("JQ_FILE_READ_PATTERN"), pattern);
            processEnvironment.insert(QStringLiteral("JQ_FILE_READ_MAX_MATCHES"), QString::number(maxMatches));
            processEnvironment.insert(
                QStringLiteral("JQ_FILE_READ_CASE_SENSITIVE"),
                caseSensitive ? QStringLiteral("1") : QStringLiteral("0")
            );
            processEnvironment.insert(
                QStringLiteral("JQ_FILE_READ_INCLUDE_HIDDEN"),
                includeHidden ? QStringLiteral("1") : QStringLiteral("0")
            );
            processEnvironment.insert(
                QStringLiteral("JQ_FILE_READ_LITERAL"),
                literal ? QStringLiteral("1") : QStringLiteral("0")
            );

            QStringList fallbackArguments;
            fallbackArguments << QStringLiteral("-NoLogo")
                              << QStringLiteral("-NoProfile")
                              << QStringLiteral("-NonInteractive")
                              << QStringLiteral("-ExecutionPolicy")
                              << QStringLiteral("Bypass")
                              << QStringLiteral("-Command")
                              << QString::fromLatin1(readRgPowerShellFallbackScript);

            QProcess fallbackProcess;
            fallbackProcess.setProcessEnvironment(processEnvironment);
            fallbackProcess.start(QStringLiteral("powershell"), fallbackArguments);
            if ( !fallbackProcess.waitForStarted(readRgStartTimeoutMs) )
            {
                if ( error != nullptr )
                {
                    const QString startError = fallbackProcess.errorString().trimmed();
                    *error = startError.isEmpty()
                        ? QStringLiteral("file.read rg fallback failed to start")
                        : QStringLiteral("file.read rg fallback failed to start: %1").arg(startError);
                }
                return false;
            }

            if ( !fallbackProcess.waitForFinished(rgTimeoutMs) )
            {
                fallbackProcess.kill();
                fallbackProcess.waitForFinished(readRgKillWaitTimeoutMs);
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.read rg fallback timed out");
                }
                return false;
            }

            if ( fallbackProcess.exitStatus() != QProcess::NormalExit )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.read rg fallback crashed");
                }
                return false;
            }

            searchExitCode = fallbackProcess.exitCode();
            stdoutBytes = fallbackProcess.readAllStandardOutput();
            stderrBytes = fallbackProcess.readAllStandardError();
            const QString fallbackStderrText = QString::fromUtf8(stderrBytes).trimmed();
            if ( searchExitCode != 0 )
            {
                if ( error != nullptr )
                {
                    *error = fallbackStderrText.isEmpty()
                        ? QStringLiteral("file.read rg fallback failed")
                        : QStringLiteral("file.read rg fallback failed: %1").arg(fallbackStderrText);
                }
                return false;
            }
        }

        const QString stderrText = usePowerShellFallback
            ? QString::fromUtf8(stderrBytes).trimmed()
            : QString::fromLocal8Bit(stderrBytes).trimmed();

        QJsonArray matches;
        QSet<QString> matchedFiles;
        bool truncated = false;
        if ( usePowerShellFallback )
        {
            QJsonParseError fallbackParseError;
            const QJsonDocument fallbackDocument =
                QJsonDocument::fromJson(stdoutBytes, &fallbackParseError);
            if ( fallbackParseError.error != QJsonParseError::NoError ||
                 !fallbackDocument.isObject() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral(
                        "file.read rg fallback returned invalid JSON: %1"
                    ).arg(fallbackParseError.errorString());
                }
                return false;
            }

            const QJsonObject fallbackObject = fallbackDocument.object();
            const QJsonArray fallbackMatches = fallbackObject.value(QStringLiteral("matches")).toArray();
            for ( const QJsonValue &matchValue : fallbackMatches )
            {
                if ( matches.size() >= maxMatches )
                {
                    truncated = true;
                    break;
                }
                if ( !matchValue.isObject() )
                {
                    continue;
                }

                const QJsonObject fallbackMatch = matchValue.toObject();
                QString matchPath = fallbackMatch.value(QStringLiteral("path")).toString().trimmed();
                if ( matchPath.isEmpty() )
                {
                    matchPath = fileInfo.absoluteFilePath();
                }
                matchedFiles.insert(matchPath);

                QString lineText = fallbackMatch.value(QStringLiteral("lineText")).toString();
                while ( lineText.endsWith(QLatin1Char('\n')) ||
                        lineText.endsWith(QLatin1Char('\r')) )
                {
                    lineText.chop(1);
                }

                int columnStart = fallbackMatch.value(QStringLiteral("columnStart")).toInt(1);
                if ( columnStart <= 0 )
                {
                    columnStart = 1;
                }
                int columnEnd = fallbackMatch.value(QStringLiteral("columnEnd")).toInt(columnStart);
                if ( columnEnd < columnStart )
                {
                    columnEnd = columnStart;
                }

                QJsonObject matchItem;
                matchItem.insert(QStringLiteral("path"), matchPath);
                matchItem.insert(QStringLiteral("lineNumber"), fallbackMatch.value(QStringLiteral("lineNumber")).toInt());
                matchItem.insert(QStringLiteral("columnStart"), columnStart);
                matchItem.insert(QStringLiteral("columnEnd"), columnEnd);
                matchItem.insert(QStringLiteral("lineText"), lineText);
                matchItem.insert(QStringLiteral("matchText"), fallbackMatch.value(QStringLiteral("matchText")).toString());
                matches.append(matchItem);
            }

            if ( fallbackObject.value(QStringLiteral("truncated")).toBool() )
            {
                truncated = true;
            }
        }
        else
        {
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
        out.insert(QStringLiteral("searchBackend"), searchBackend);
        out.insert(QStringLiteral("searchExitCode"), searchExitCode);
        if ( !stderrText.isEmpty() )
        {
            out.insert(QStringLiteral("stderr"), stderrText);
        }
        out.insert(QStringLiteral("matches"), matches);

        *result = out;
        qInfo().noquote() << QStringLiteral(
            "[capability.file.read] rg done path=%1 backend=%2 matches=%3 files=%4 exitCode=%5"
        ).arg(
            fileInfo.absoluteFilePath(),
            searchBackend,
            QString::number(matches.size()),
            QString::number(matchedFiles.size()),
            QString::number(searchExitCode)
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

    Common::ContentEncoding encoding = Common::ContentEncoding::Utf8;
    if ( !Common::parseEncoding(
            paramsObject,
            QStringLiteral("encoding"),
            &encoding,
            &parseError
        ) )
    {
        return Common::failInvalidParams(invalidParams, error, parseError);
    }

    qint64 maxBytes = defaultReadMaxBytes;
    if ( !parseReadMaxBytes(paramsObject, &maxBytes, &parseError) )
    {
        return Common::failInvalidParams(invalidParams, error, parseError);
    }

    qint64 offsetBytes = 0;
    if ( !parseReadOffsetBytes(paramsObject, &offsetBytes, &parseError) )
    {
        return Common::failInvalidParams(invalidParams, error, parseError);
    }
    if ( offsetBytes > fileInfo.size() )
    {
        return Common::failInvalidParams(
            invalidParams,
            error,
            QStringLiteral("offsetBytes must be integer within [0, %1]").arg(fileInfo.size())
        );
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.file.read] start path=%1 offsetBytes=%2 maxBytes=%3 encoding=%4"
    ).arg(
        fileInfo.absoluteFilePath(),
        QString::number(offsetBytes),
        QString::number(maxBytes),
        Common::encodingName(encoding)
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
        if ( chunk.isEmpty() )
        {
            if ( file.error() != QFileDevice::NoError )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("file.read read failed: %1").arg(file.errorString().trimmed());
                }
                return false;
            }
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
    out.insert(QStringLiteral("encoding"), Common::encodingName(encoding));
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

