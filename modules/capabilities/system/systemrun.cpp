// .h include
#include "capabilities/system/systemrun.h"

// Qt lib import
#include <QDebug>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
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
const QSet<QString> unsafeEnvironmentKeys =
{
    QStringLiteral("LD_PRELOAD"),
    QStringLiteral("LD_LIBRARY_PATH"),
    QStringLiteral("DYLD_INSERT_LIBRARIES"),
    QStringLiteral("DYLD_LIBRARY_PATH"),
    QStringLiteral("DYLD_FRAMEWORK_PATH"),
    QStringLiteral("DYLD_FORCE_FLAT_NAMESPACE"),
    QStringLiteral("NODE_OPTIONS"),
    QStringLiteral("RUBYOPT"),
    QStringLiteral("PERL5OPT"),
    QStringLiteral("PYTHONHOME"),
    QStringLiteral("PYTHONPATH"),
    QStringLiteral("BASH_ENV"),
    QStringLiteral("ENV"),
    QStringLiteral("SHELLOPTS"),
    QStringLiteral("PS4"),
};

bool parseCommand(
    const QJsonObject &paramsObject,
    QString *program,
    QStringList *arguments,
    QString *error
)
{
    if ( ( program == nullptr ) || ( arguments == nullptr ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: command output pointer is null");
        }
        return false;
    }

    program->clear();
    arguments->clear();

    const QJsonValue commandValue = paramsObject.value(QStringLiteral("command"));
    if ( commandValue.isUndefined() || commandValue.isNull() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run command is required");
        }
        return false;
    }

    if ( !commandValue.isArray() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run command must be string array");
        }
        return false;
    }

    QStringList commandItems;
    if ( !Common::parseOptionalStringArray(
            paramsObject,
            QStringLiteral("command"),
            &commandItems,
            error,
            QStringLiteral("system.run")
        ) )
    {
        return false;
    }

    if ( commandItems.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run command must not be empty");
        }
        return false;
    }

    for ( int i = 0; i < commandItems.size(); ++i )
    {
        const QString itemText = commandItems.at(i);
        if ( i == 0 )
        {
            const QString parsedProgram = itemText.trimmed();
            if ( parsedProgram.isEmpty() )
            {
                if ( error != nullptr )
                {
                    *error = QStringLiteral("system.run command[0] must not be empty");
                }
                return false;
            }
            *program = parsedProgram;
            continue;
        }
        arguments->append(itemText);
    }

    return true;
}

bool parseRawCommand(
    const QJsonObject &paramsObject,
    QString *rawCommand,
    QString *error
)
{
    return Common::parseOptionalTrimmedString(
        paramsObject,
        QStringLiteral("rawCommand"),
        rawCommand,
        error,
        QStringLiteral("system.run")
    );
}

bool parseNeedsScreenRecording(
    const QJsonObject &paramsObject,
    bool *needsScreenRecording,
    QString *error
)
{
    if ( needsScreenRecording == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: needsScreenRecording output pointer is null");
        }
        return false;
    }

    return Common::parseOptionalBool(
        paramsObject,
        QStringLiteral("needsScreenRecording"),
        false,
        needsScreenRecording,
        error,
        QStringLiteral("system.run")
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
        QStringLiteral("env"),
        QString(),
        true,
        environment,
        error,
        QStringLiteral("system.run"),
        true,
        &unsafeEnvironmentKeys,
        QStringLiteral("[capability.system.run]")
    );
}

bool parseExecuteRequest(
    const QJsonValue &params,
    QString *program,
    QStringList *arguments,
    QString *rawCommand,
    QString *workingDirectory,
    int *timeoutMs,
    bool *needsScreenRecording,
    QProcessEnvironment *environment,
    QString *error
)
{
    if ( ( program == nullptr ) ||
         ( arguments == nullptr ) ||
         ( rawCommand == nullptr ) ||
         ( workingDirectory == nullptr ) ||
         ( timeoutMs == nullptr ) ||
         ( needsScreenRecording == nullptr ) ||
         ( environment == nullptr ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.run internal error: output pointer is null");
        }
        return false;
    }

    QJsonObject paramsObject;
    if ( !Common::parseParamsObject(
            params,
            &paramsObject,
            error,
            QStringLiteral("system.run")
        ) )
    {
        return false;
    }
    if ( !parseCommand(paramsObject, program, arguments, error) )
    {
        return false;
    }
    if ( !parseRawCommand(paramsObject, rawCommand, error) )
    {
        return false;
    }
    if ( !Common::parseTimeoutMs(
            paramsObject,
            QStringLiteral("timeoutMs"),
            processDefaultTimeoutMs,
            processMinTimeoutMs,
            processMaxTimeoutMs,
            timeoutMs,
            error,
            QStringLiteral("system.run")
        ) )
    {
        return false;
    }
    if ( !parseNeedsScreenRecording(paramsObject, needsScreenRecording, error) )
    {
        return false;
    }
    if ( !parseEnvironment(paramsObject, environment, error) )
    {
        return false;
    }

    if ( !Common::parseOptionalTrimmedString(
            paramsObject,
            QStringLiteral("cwd"),
            workingDirectory,
            error,
            QStringLiteral("system.run")
        ) )
    {
        return false;
    }

    return true;
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
    QString rawCommand;
    QString workingDirectory;
    int timeoutMs = processDefaultTimeoutMs;
    bool needsScreenRecording = false;
    QProcessEnvironment environment;
    QString parseError;
    if ( !parseExecuteRequest(
            params,
            &program,
            &arguments,
            &rawCommand,
            &workingDirectory,
            &timeoutMs,
            &needsScreenRecording,
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

    if ( invokeTimeoutMs >= 0 )
    {
        timeoutMs = qMin(timeoutMs, invokeTimeoutMs);
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.system.run] start program=%1 args=%2 timeoutMs=%3 workingDirectory=%4 rawCommand=%5 needsScreenRecording=%6"
    ).arg(
        program,
        arguments.join(' '),
        QString::number(timeoutMs),
        workingDirectory,
        rawCommand,
        needsScreenRecording ? QStringLiteral("true") : QStringLiteral("false")
    );

    QProcess process;
    process.setProcessEnvironment(environment);
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
    out.insert(QStringLiteral("rawCommand"), rawCommand);
    out.insert(QStringLiteral("workingDirectory"), workingDirectory);
    out.insert(QStringLiteral("timeoutMs"), timeoutMs);
    out.insert(QStringLiteral("elapsedMs"), static_cast<int>(timer.elapsed()));
    out.insert(QStringLiteral("detached"), false);
    out.insert(QStringLiteral("needsScreenRecording"), needsScreenRecording);
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
        "[capability.system.run] done program=%1 exitCode=%2 timedOut=%3 elapsedMs=%4"
    ).arg(
        program,
        QString::number(process.exitCode()),
        timedOut ? QStringLiteral("true") : QStringLiteral("false"),
        QString::number(timer.elapsed())
    );
    return true;
}
