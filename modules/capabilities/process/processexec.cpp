// .h include
#include "capabilities/process/processexec.h"

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
            *error = QStringLiteral("process.exec internal error: arguments output pointer is null");
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
            *error = QStringLiteral("process.exec arguments must be string array");
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
                *error = QStringLiteral("process.exec arguments[%1] must be string").arg(i);
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
            *error = QStringLiteral("process.exec internal error: bool output pointer is null");
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
            *error = QStringLiteral("process.exec %1 must be boolean").arg(field);
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
            *error = QStringLiteral("process.exec internal error: timeout output pointer is null");
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
            *error = QStringLiteral("process.exec timeoutMs must be number");
        }
        return false;
    }

    bool ok = false;
    const int parsedTimeoutMs = QString::number(timeoutValue.toDouble(), 'g', 16).toInt(&ok);
    if ( !ok )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.exec timeoutMs is invalid");
        }
        return false;
    }
    if ( ( parsedTimeoutMs < processMinTimeoutMs ) ||
         ( parsedTimeoutMs > processMaxTimeoutMs ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "process.exec timeoutMs out of range [%1, %2]"
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
            *error = QStringLiteral("process.exec internal error: environment output pointer is null");
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
            *error = QStringLiteral("process.exec environment must be object");
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
                *error = QStringLiteral("process.exec environment contains empty key");
            }
            return false;
        }
        if ( !it.value().isString() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("process.exec environment key \"%1\" must be string value").arg(key);
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
         ( mergeChannels == nullptr ) ||
         ( environment == nullptr ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.exec internal error: output pointer is null");
        }
        return false;
    }

    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.exec params must be object");
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
                "process.exec command mode is not supported; use program and arguments"
            );
        }
        return false;
    }

    arguments->clear();
    if ( programValue.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.exec requires program");
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
            *error = QStringLiteral("process.exec stdin must be string");
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

    return parseOptionalBool(
        paramsObject,
        QStringLiteral("mergeChannels"),
        false,
        mergeChannels,
        error
    );
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

bool ProcessExec::execute(
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
            *error = QStringLiteral("process.exec output pointer is null");
        }
        return false;
    }

    QString program;
    QStringList arguments;
    QString workingDirectory;
    QByteArray stdinBytes;
    int timeoutMs = processDefaultTimeoutMs;
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

    if ( invokeTimeoutMs > 0 )
    {
        timeoutMs = qMax(1, qMin(timeoutMs, invokeTimeoutMs));
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.process.exec] start program=%1 args=%2 timeoutMs=%3 workingDirectory=%4"
    ).arg(
        program,
        arguments.join(' '),
        QString::number(timeoutMs),
        workingDirectory
    );

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
                ? QStringLiteral("process.exec failed to start process")
                : QStringLiteral("process.exec failed to start process: %1").arg(startError);
        }
        qWarning().noquote() << QStringLiteral(
            "[capability.process.exec] failed to start program=%1 error=%2"
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
        "[capability.process.exec] done program=%1 exitCode=%2 timedOut=%3 elapsedMs=%4"
    ).arg(
        program,
        QString::number(process.exitCode()),
        timedOut ? QStringLiteral("true") : QStringLiteral("false"),
        QString::number(timer.elapsed())
    );
    return true;
}
