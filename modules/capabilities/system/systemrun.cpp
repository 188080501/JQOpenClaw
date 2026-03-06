// .h include
#include "capabilities/system/systemrun.h"

// Qt lib import
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QtGlobal>

namespace
{
const int processStartTimeoutMs = 5000;
const int processDefaultTimeoutMs = 30000;
const int processMinTimeoutMs = 100;
const int processMaxTimeoutMs = 300000;
const int processKillWaitTimeoutMs = 3000;

QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

bool parseArguments(
    const QJsonObject &paramsObject,
    QStringList *arguments,
    QString *error
)
{
    if ( arguments == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: arguments output pointer is null");
        }
        return false;
    }

    arguments->clear();
    const QJsonValue argumentsValue = paramsObject.value(QStringLiteral("arguments"));
    if ( argumentsValue.isUndefined() || argumentsValue.isNull() )
    {
        return true;
    }
    if ( !argumentsValue.isArray() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run arguments must be string array");
        }
        return false;
    }

    const QJsonArray argumentsArray = argumentsValue.toArray();
    for ( int i = 0; i < argumentsArray.size(); ++i )
    {
        const QJsonValue item = argumentsArray.at(i);
        if ( !item.isString() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.run arguments[%1] must be string").arg(i);
            }
            return false;
        }
        arguments->append(item.toString());
    }
    return true;
}

bool parseOptionalBool(
    const QJsonObject &paramsObject,
    const QString &field,
    bool defaultValue,
    bool *value,
    QString *error
)
{
    if ( value == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: bool output pointer is null");
        }
        return false;
    }

    *value = defaultValue;
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        return true;
    }
    if ( !rawValue.isBool() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run %1 must be boolean").arg(field);
        }
        return false;
    }

    *value = rawValue.toBool();
    return true;
}

bool parseTimeoutMs(
    const QJsonObject &paramsObject,
    int *timeoutMs,
    QString *error
)
{
    if ( timeoutMs == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: timeout output pointer is null");
        }
        return false;
    }

    *timeoutMs = processDefaultTimeoutMs;
    const QJsonValue timeoutValue = paramsObject.value(QStringLiteral("timeoutMs"));
    if ( timeoutValue.isUndefined() || timeoutValue.isNull() )
    {
        return true;
    }
    if ( !timeoutValue.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run timeoutMs must be number");
        }
        return false;
    }

    bool ok = false;
    const int parsedTimeoutMs = QString::number(timeoutValue.toDouble(), 'g', 16).toInt(&ok);
    if ( !ok )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run timeoutMs is invalid");
        }
        return false;
    }
    if ( ( parsedTimeoutMs < processMinTimeoutMs ) ||
         ( parsedTimeoutMs > processMaxTimeoutMs ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "system.run timeoutMs out of range [%1, %2]"
            ).arg(processMinTimeoutMs).arg(processMaxTimeoutMs);
        }
        return false;
    }

    *timeoutMs = parsedTimeoutMs;
    return true;
}

bool parseEnvironment(
    const QJsonObject &paramsObject,
    QProcessEnvironment *environment,
    QString *error
)
{
    if ( environment == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: environment output pointer is null");
        }
        return false;
    }

    bool inheritEnvironment = true;
    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("inheritEnvironment"),
            true,
            &inheritEnvironment,
            error
        ) )
    {
        return false;
    }

    *environment = inheritEnvironment
        ? QProcessEnvironment::systemEnvironment()
        : QProcessEnvironment();

    const QJsonValue environmentValue = paramsObject.value(QStringLiteral("environment"));
    if ( environmentValue.isUndefined() || environmentValue.isNull() )
    {
        return true;
    }
    if ( !environmentValue.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run environment must be object");
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
                *error = QStringLiteral("system.run environment contains empty key");
            }
            return false;
        }
        if ( !it.value().isString() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.run environment key \"%1\" must be string value").arg(key);
            }
            return false;
        }
        environment->insert(key, it.value().toString());
    }
    return true;
}

bool parseExecuteRequest(
    const QJsonValue &params,
    QString *program,
    QStringList *arguments,
    QString *workingDirectory,
    QByteArray *stdinBytes,
    int *timeoutMs,
    bool *detached,
    bool *mergeChannels,
    QProcessEnvironment *environment,
    QString *error
)
{
    if ( ( program == nullptr ) ||
         ( arguments == nullptr ) ||
         ( workingDirectory == nullptr ) ||
         ( stdinBytes == nullptr ) ||
         ( timeoutMs == nullptr ) ||
         ( detached == nullptr ) ||
         ( mergeChannels == nullptr ) ||
         ( environment == nullptr ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: output pointer is null");
        }
        return false;
    }

    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run params must be object");
        }
        return false;
    }

    const QJsonObject paramsObject = params.toObject();
    const QString command = extractString(paramsObject, QStringLiteral("command"));
    const QString programValue = extractString(paramsObject, QStringLiteral("program"));
    if ( !command.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "system.run command mode is not supported; use program and arguments"
            );
        }
        return false;
    }

    arguments->clear();
    if ( programValue.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run requires program");
        }
        return false;
    }

    *program = programValue;
    if ( !parseArguments(paramsObject, arguments, error) )
    {
        return false;
    }

    *workingDirectory = extractString(paramsObject, QStringLiteral("workingDirectory"));

    const QJsonValue stdinValue = paramsObject.value(QStringLiteral("stdin"));
    if ( stdinValue.isUndefined() || stdinValue.isNull() )
    {
        stdinBytes->clear();
    }
    else if ( stdinValue.isString() )
    {
        *stdinBytes = stdinValue.toString().toUtf8();
    }
    else
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run stdin must be string");
        }
        return false;
    }

    if ( !parseTimeoutMs(paramsObject, timeoutMs, error) )
    {
        return false;
    }
    if ( !parseEnvironment(paramsObject, environment, error) )
    {
        return false;
    }
    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("detached"),
            false,
            detached,
            error
        ) )
    {
        return false;
    }

    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("mergeChannels"),
            false,
            mergeChannels,
            error
        ) )
    {
        return false;
    }

    if ( *detached )
    {
        if ( *mergeChannels )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.run mergeChannels is not supported when detached is true"
                );
            }
            return false;
        }
        if ( !stdinBytes->isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.run stdin is not supported when detached is true"
                );
            }
            return false;
        }
    }

    return true;
}

QJsonArray toJsonArray(const QStringList &items)
{
    QJsonArray out;
    for ( const QString &item : items )
    {
        out.append(item);
    }
    return out;
}

QString processExitStatusName(QProcess::ExitStatus exitStatus)
{
    if ( exitStatus == QProcess::NormalExit )
    {
        return QStringLiteral("normal");
    }
    return QStringLiteral("crash");
}

QString processErrorName(QProcess::ProcessError processError)
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

bool hasProcessError(QProcess::ProcessError processError)
{
    return processError != QProcess::UnknownError;
}

QString processResultClass(bool timedOut, QProcess::ExitStatus exitStatus, int exitCode)
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
}

bool SystemRun::execute(
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
            *error = QStringLiteral("system.run output pointer is null");
        }
        return false;
    }

    QString program;
    QStringList arguments;
    QString workingDirectory;
    QByteArray stdinBytes;
    int timeoutMs = processDefaultTimeoutMs;
    bool detached = false;
    bool mergeChannels = false;
    QProcessEnvironment environment;
    QString parseError;
    if ( !parseExecuteRequest(
            params,
            &program,
            &arguments,
            &workingDirectory,
            &stdinBytes,
            &timeoutMs,
            &detached,
            &mergeChannels,
            &environment,
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

    if ( ( !detached ) && ( invokeTimeoutMs >= 0 ) )
    {
        timeoutMs = qMin(timeoutMs, invokeTimeoutMs);
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.system.run] start program=%1 args=%2 timeoutMs=%3 detached=%4 workingDirectory=%5"
    ).arg(
        program,
        arguments.join(' '),
        QString::number(timeoutMs),
        detached ? QStringLiteral("true") : QStringLiteral("false"),
        workingDirectory
    );

    if ( detached )
    {
        QElapsedTimer timer;
        timer.start();

        QProcess process;
        process.setProcessEnvironment(environment);
        if ( !workingDirectory.isEmpty() )
        {
            process.setWorkingDirectory(workingDirectory);
        }
        process.setProgram(program);
        process.setArguments(arguments);

        qint64 detachedPid = 0;
        const bool detachedStarted = process.startDetached(&detachedPid);
        if ( !detachedStarted )
        {
            const QString startError = process.errorString().trimmed();
            if ( error != nullptr )
            {
                *error = startError.isEmpty()
                    ? QStringLiteral("system.run failed to start detached process")
                    : QStringLiteral("system.run failed to start detached process: %1")
                          .arg(startError);
            }
            qWarning().noquote() << QStringLiteral(
                "[capability.system.run] failed to start detached program=%1 error=%2"
            ).arg(program, startError);
            return false;
        }

        QJsonObject out;
        out.insert(QStringLiteral("program"), program);
        out.insert(QStringLiteral("arguments"), toJsonArray(arguments));
        out.insert(QStringLiteral("workingDirectory"), workingDirectory);
        out.insert(QStringLiteral("timeoutMs"), timeoutMs);
        out.insert(QStringLiteral("elapsedMs"), static_cast<int>(timer.elapsed()));
        out.insert(QStringLiteral("detached"), true);
        if ( detachedPid > 0 )
        {
            out.insert(QStringLiteral("pid"), static_cast<double>(detachedPid));
        }
        out.insert(QStringLiteral("timedOut"), false);
        out.insert(QStringLiteral("exitCode"), -1);
        out.insert(QStringLiteral("exitStatus"), QStringLiteral("detached"));
        out.insert(QStringLiteral("stdout"), QString());
        out.insert(QStringLiteral("stderr"), QString());
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("resultClass"), QStringLiteral("detached"));

        *result = out;
        qInfo().noquote() << QStringLiteral(
            "[capability.system.run] detached program=%1 pid=%2 elapsedMs=%3"
        ).arg(
            program,
            detachedPid > 0 ? QString::number(detachedPid) : QStringLiteral("unknown"),
            QString::number(timer.elapsed())
        );
        return true;
    }

    QProcess process;
    process.setProcessEnvironment(environment);
    if ( mergeChannels )
    {
        process.setProcessChannelMode(QProcess::MergedChannels);
    }
    if ( !workingDirectory.isEmpty() )
    {
        process.setWorkingDirectory(workingDirectory);
    }

    QElapsedTimer timer;
    timer.start();
    process.start(program, arguments);
    if ( !process.waitForStarted(processStartTimeoutMs) )
    {
        const QString startError = process.errorString().trimmed();
        if ( error != nullptr )
        {
            *error = startError.isEmpty()
                ? QStringLiteral("system.run failed to start process")
                : QStringLiteral("system.run failed to start process: %1").arg(startError);
        }
        qWarning().noquote() << QStringLiteral(
            "[capability.system.run] failed to start program=%1 error=%2"
        ).arg(program, startError);
        return false;
    }

    if ( !stdinBytes.isEmpty() )
    {
        process.write(stdinBytes);
    }
    process.closeWriteChannel();

    const bool finishedWithinTimeout = process.waitForFinished(timeoutMs);
    const bool timedOut = !finishedWithinTimeout;
    if ( timedOut )
    {
        process.kill();
        process.waitForFinished(processKillWaitTimeoutMs);
    }

    const QByteArray stdoutBytes = process.readAllStandardOutput();
    const QByteArray stderrBytes = process.readAllStandardError();
    const QProcess::ExitStatus exitStatus = process.exitStatus();
    const int exitCode = process.exitCode();
    const QProcess::ProcessError processError = process.error();
    const bool processHasError = hasProcessError(processError);
    const QString resultClass = processResultClass(timedOut, exitStatus, exitCode);
    const bool ok = ( resultClass == QStringLiteral("ok") );

    QJsonObject out;
    out.insert(QStringLiteral("program"), program);
    out.insert(QStringLiteral("arguments"), toJsonArray(arguments));
    out.insert(QStringLiteral("workingDirectory"), workingDirectory);
    out.insert(QStringLiteral("timeoutMs"), timeoutMs);
    out.insert(QStringLiteral("elapsedMs"), static_cast<int>(timer.elapsed()));
    out.insert(QStringLiteral("detached"), false);
    out.insert(QStringLiteral("timedOut"), timedOut);
    out.insert(QStringLiteral("exitCode"), exitCode);
    out.insert(
        QStringLiteral("exitStatus"),
        processExitStatusName(exitStatus)
    );
    out.insert(QStringLiteral("stdout"), QString::fromLocal8Bit(stdoutBytes));
    out.insert(QStringLiteral("stderr"), QString::fromLocal8Bit(stderrBytes));
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("resultClass"), resultClass);
    if ( processHasError )
    {
        out.insert(QStringLiteral("processError"), static_cast<int>(processError));
        out.insert(QStringLiteral("processErrorName"), processErrorName(processError));
        out.insert(QStringLiteral("processErrorString"), process.errorString().trimmed());
    }

    *result = out;
    qInfo().noquote() << QStringLiteral(
        "[capability.system.run] done program=%1 exitCode=%2 timedOut=%3 elapsedMs=%4"
    ).arg(
        program,
        QString::number(process.exitCode()),
        timedOut ? QStringLiteral("true") : QStringLiteral("false"),
        QString::number(timer.elapsed())
    );
    return true;
}
