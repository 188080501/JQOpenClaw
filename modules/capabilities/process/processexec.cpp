// .h include
#include "capabilities/process/processexec.h"

// Qt lib import
#include <QDebug>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QtGlobal>

// JQOpenClaw import
#include "common/common.h"

namespace
{
const int processStartTimeoutMs = 5000;
const int processDefaultTimeoutMs = 30000;
const int processMinTimeoutMs = 100;
const int processMaxTimeoutMs = 300000;
const int processKillWaitTimeoutMs = 3000;

bool parseArguments(
    const QJsonObject &paramsObject,
    QStringList *arguments,
    QString *error
)
{
    return Common::parseOptionalStringArray(
        paramsObject,
        QStringLiteral("arguments"),
        arguments,
        error,
        QStringLiteral("process.exec")
    );
}

bool parseEnvironment(
    const QJsonObject &paramsObject,
    QProcessEnvironment *environment,
    QString *error
)
{
    return Common::parseProcessEnvironment(
        paramsObject,
        QStringLiteral("environment"),
        QStringLiteral("inheritEnvironment"),
        true,
        environment,
        error,
        QStringLiteral("process.exec")
    );
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
            *error = QStringLiteral("process.exec internal error: output pointer is null");
        }
        return false;
    }

    QJsonObject paramsObject;
    if ( !Common::parseParamsObject(
            params,
            &paramsObject,
            error,
            QStringLiteral("process.exec")
        ) )
    {
        return false;
    }
    const QString command = Common::extractStringTrimmed(paramsObject, QStringLiteral("command"));
    const QString programValue = Common::extractStringTrimmed(paramsObject, QStringLiteral("program"));
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

    *workingDirectory = Common::extractStringTrimmed(paramsObject, QStringLiteral("workingDirectory"));

    QString stdinText;
    if ( !Common::parseOptionalString(
            paramsObject,
            QStringLiteral("stdin"),
            &stdinText,
            error,
            QStringLiteral("process.exec")
        ) )
    {
        return false;
    }
    *stdinBytes = stdinText.toUtf8();

    if ( !Common::parseTimeoutMs(
            paramsObject,
            QStringLiteral("timeoutMs"),
            processDefaultTimeoutMs,
            processMinTimeoutMs,
            processMaxTimeoutMs,
            timeoutMs,
            error,
            QStringLiteral("process.exec")
        ) )
    {
        return false;
    }
    if ( !parseEnvironment(paramsObject, environment, error) )
    {
        return false;
    }
    if ( !Common::parseOptionalBool(
            paramsObject,
            QStringLiteral("detached"),
            false,
            detached,
            error,
            QStringLiteral("process.exec")
        ) )
    {
        return false;
    }

    if ( !Common::parseOptionalBool(
            paramsObject,
            QStringLiteral("mergeChannels"),
            false,
            mergeChannels,
            error,
            QStringLiteral("process.exec")
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
                    "process.exec mergeChannels is not supported when detached is true"
                );
            }
            return false;
        }
        if ( !stdinBytes->isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "process.exec stdin is not supported when detached is true"
                );
            }
            return false;
        }
    }

    return true;
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
        "[capability.process.exec] start program=%1 args=%2 timeoutMs=%3 detached=%4 workingDirectory=%5"
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
                    ? QStringLiteral("process.exec failed to start detached process")
                    : QStringLiteral("process.exec failed to start detached process: %1")
                          .arg(startError);
            }
            qWarning().noquote() << QStringLiteral(
                "[capability.process.exec] failed to start detached program=%1 error=%2"
            ).arg(program, startError);
            return false;
        }

        QJsonObject out;
        out.insert(QStringLiteral("program"), program);
        out.insert(QStringLiteral("arguments"), Common::toJsonArray(arguments));
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
            "[capability.process.exec] detached program=%1 pid=%2 elapsedMs=%3"
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
    const bool processHasError = Common::hasProcessError(processError);
    const QString resultClass = Common::processResultClass(timedOut, exitStatus, exitCode);
    const bool ok = ( resultClass == QStringLiteral("ok") );

    QJsonObject out;
    out.insert(QStringLiteral("program"), program);
    out.insert(QStringLiteral("arguments"), Common::toJsonArray(arguments));
    out.insert(QStringLiteral("workingDirectory"), workingDirectory);
    out.insert(QStringLiteral("timeoutMs"), timeoutMs);
    out.insert(QStringLiteral("elapsedMs"), static_cast<int>(timer.elapsed()));
    out.insert(QStringLiteral("detached"), false);
    out.insert(QStringLiteral("timedOut"), timedOut);
    out.insert(QStringLiteral("exitCode"), exitCode);
    out.insert(
        QStringLiteral("exitStatus"),
        Common::processExitStatusName(exitStatus)
    );
    out.insert(QStringLiteral("stdout"), QString::fromLocal8Bit(stdoutBytes));
    out.insert(QStringLiteral("stderr"), QString::fromLocal8Bit(stderrBytes));
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("resultClass"), resultClass);
    if ( processHasError )
    {
        out.insert(QStringLiteral("processError"), static_cast<int>(processError));
        out.insert(QStringLiteral("processErrorName"), Common::processErrorName(processError));
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

