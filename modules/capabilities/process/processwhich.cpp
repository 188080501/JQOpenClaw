// .h include
#include "capabilities/process/processwhich.h"

// Qt lib import
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>

// JQOpenClaw import
#include "common/common.h"

namespace
{
const int processWhichStartTimeoutMs = 3000;
const int processWhichTimeoutMs = 5000;
const int processWhichKillWaitTimeoutMs = 1000;
const int processWhichMaxPrograms = 200;

QString normalizeProgramKey(const QString &program)
{
#ifdef Q_OS_WIN
    return program.trimmed().toLower();
#else
    return program.trimmed();
#endif
}

QString normalizePathKey(const QString &path)
{
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(path).toLower();
#else
    return QDir::fromNativeSeparators(path);
#endif
}

QStringList uniquePaths(const QStringList &paths)
{
    QStringList out;
    QSet<QString> seen;
    for ( const QString &rawPath : paths )
    {
        QString candidatePath = rawPath.trimmed();
        if ( ( candidatePath.size() >= 2 ) &&
             candidatePath.startsWith('"') &&
             candidatePath.endsWith('"') )
        {
            candidatePath = candidatePath.mid(1, candidatePath.size() - 2);
        }

        const QFileInfo fileInfo(candidatePath);
        if ( !fileInfo.exists() || !fileInfo.isFile() )
        {
            continue;
        }

        const QString absolutePath = fileInfo.absoluteFilePath();
        const QString pathKey = normalizePathKey(absolutePath);
        if ( seen.contains(pathKey) )
        {
            continue;
        }

        seen.insert(pathKey);
        out.append(absolutePath);
    }
    return out;
}

QJsonArray toJsonArray(const QStringList &items)
{
    return Common::toJsonArray(items);
}

QStringList splitLines(const QByteArray &bytes)
{
    QString output = QString::fromLocal8Bit(bytes);
    output.replace('\r', '\n');
    return output.split('\n', Qt::SkipEmptyParts);
}

bool parsePrograms(
    const QJsonValue &params,
    QStringList *programs,
    QString *error
)
{
    if ( programs == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.which internal error: programs output pointer is null");
        }
        return false;
    }

    programs->clear();
    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.which params must be object");
        }
        return false;
    }

    const QJsonObject paramsObject = params.toObject();
    const QJsonValue programValue = paramsObject.value(QStringLiteral("program"));
    if ( !programValue.isUndefined() && !programValue.isNull() )
    {
        if ( !programValue.isString() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("process.which program must be string");
            }
            return false;
        }

        const QString program = programValue.toString().trimmed();
        if ( program.isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("process.which program is empty");
            }
            return false;
        }
        programs->append(program);
    }

    const QJsonValue programsValue = paramsObject.value(QStringLiteral("programs"));
    if ( !programsValue.isUndefined() && !programsValue.isNull() )
    {
        if ( !programsValue.isArray() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("process.which programs must be string array");
            }
            return false;
        }

        const QJsonArray programsArray = programsValue.toArray();
        for ( int i = 0; i < programsArray.size(); ++i )
        {
            const QJsonValue item = programsArray.at(i);
            if ( !item.isString() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("process.which programs[%1] must be string").arg(i);
                }
                return false;
            }

            const QString program = item.toString().trimmed();
            if ( program.isEmpty() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("process.which programs[%1] is empty").arg(i);
                }
                return false;
            }

            programs->append(program);
        }
    }

    if ( programs->isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.which requires program or programs");
        }
        return false;
    }

    QStringList dedupedPrograms;
    QSet<QString> seen;
    for ( const QString &program : *programs )
    {
        const QString key = normalizeProgramKey(program);
        if ( seen.contains(key) )
        {
            continue;
        }
        seen.insert(key);
        dedupedPrograms.append(program);
    }
    *programs = dedupedPrograms;

    if ( programs->size() > processWhichMaxPrograms )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "process.which programs count out of range [1, %1]"
            ).arg(processWhichMaxPrograms);
        }
        return false;
    }

    return true;
}

bool runBackendLookup(
    const QString &program,
    QStringList *paths,
    QString *backend
)
{
    if ( ( paths == nullptr ) || ( backend == nullptr ) )
    {
        return false;
    }

    paths->clear();
    backend->clear();
    QString backendProgram;
    QStringList backendArguments;
#ifdef Q_OS_WIN
    backendProgram = QStringLiteral("where");
    backendArguments.append(program);
#else
    backendProgram = QStringLiteral("which");
    backendArguments.append(QStringLiteral("-a"));
    backendArguments.append(program);
#endif

    QProcess process;
    process.start(backendProgram, backendArguments);
    if ( !process.waitForStarted(processWhichStartTimeoutMs) )
    {
        return false;
    }

    if ( !process.waitForFinished(processWhichTimeoutMs) )
    {
        process.kill();
        process.waitForFinished(processWhichKillWaitTimeoutMs);
        return false;
    }

    *backend = backendProgram;
    const QStringList candidates = splitLines(process.readAllStandardOutput());
    QStringList resolvedPaths;
    for ( const QString &candidate : candidates )
    {
        resolvedPaths.append(candidate.trimmed());
    }

    *paths = uniquePaths(resolvedPaths);
    return true;
}

QStringList fallbackFindExecutable(const QString &program)
{
    const QString resolvedPath = QStandardPaths::findExecutable(program);
    if ( resolvedPath.isEmpty() )
    {
        return QStringList();
    }
    return uniquePaths(QStringList() << resolvedPath);
}

QJsonObject buildItem(
    const QString &program,
    const QStringList &paths,
    const QString &backend
)
{
    QJsonObject out;
    out.insert(QStringLiteral("program"), program);
    out.insert(QStringLiteral("backend"), backend);
    out.insert(QStringLiteral("found"), !paths.isEmpty());
    out.insert(QStringLiteral("allPaths"), toJsonArray(paths));
    if ( !paths.isEmpty() )
    {
        out.insert(QStringLiteral("path"), paths.first());
    }
    return out;
}
}

bool ProcessWhich::execute(
    const QJsonValue &params,
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
            *error = QStringLiteral("process.which output pointer is null");
        }
        return false;
    }

    QStringList programs;
    QString parseError;
    if ( !parsePrograms(params, &programs, &parseError) )
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
        "[capability.process.which] start programs=%1"
    ).arg(programs.join(QStringLiteral(", ")));

    int foundCount = 0;
    QJsonArray items;
    for ( const QString &program : programs )
    {
        QStringList paths;
        QString backend;
        runBackendLookup(program, &paths, &backend);
        if ( paths.isEmpty() )
        {
            const QStringList fallbackPaths = fallbackFindExecutable(program);
            if ( !fallbackPaths.isEmpty() )
            {
                paths = fallbackPaths;
                backend = QStringLiteral("qt.findExecutable");
            }
            else if ( backend.trimmed().isEmpty() )
            {
                backend = QStringLiteral("qt.findExecutable");
            }
        }

        const QJsonObject item = buildItem(program, paths, backend);
        if ( item.value(QStringLiteral("found")).toBool(false) )
        {
            ++foundCount;
        }

        items.append(item);
    }

    QJsonObject out;
    out.insert(QStringLiteral("operation"), QStringLiteral("which"));
    out.insert(QStringLiteral("requestedCount"), programs.size());
    out.insert(QStringLiteral("foundCount"), foundCount);
    out.insert(
        QStringLiteral("allFound"),
        foundCount == programs.size()
    );
    out.insert(QStringLiteral("results"), items);

    if ( items.size() == 1 )
    {
        const QJsonObject firstItem = items.first().toObject();
        out.insert(QStringLiteral("program"), firstItem.value(QStringLiteral("program")));
        out.insert(QStringLiteral("backend"), firstItem.value(QStringLiteral("backend")));
        out.insert(QStringLiteral("found"), firstItem.value(QStringLiteral("found")));
        out.insert(QStringLiteral("allPaths"), firstItem.value(QStringLiteral("allPaths")));
        if ( firstItem.contains(QStringLiteral("path")) )
        {
            out.insert(QStringLiteral("path"), firstItem.value(QStringLiteral("path")));
        }
    }

    *result = out;
    qInfo().noquote() << QStringLiteral(
        "[capability.process.which] done requested=%1 found=%2"
    ).arg(programs.size()).arg(foundCount);
    return true;
}
