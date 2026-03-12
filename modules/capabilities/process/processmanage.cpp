// .h include
#include "capabilities/process/processmanage.h"

// Qt lib import
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QtGlobal>

// JQOpenClaw import
#include "common/common.h"

// C++ lib import
#include <climits>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace
{
const int processListDefaultLimit = 300;
const int processListMinLimit = 1;
const int processListMaxLimit = 5000;
const int processKillDefaultWaitMs = 3000;
const int processKillMinWaitMs = 0;
const int processKillMaxWaitMs = 30000;
// Self-kill uses a short delay so node.invoke can flush the response first.
const int processKillSelfDelayMs = 200;

enum class ProcessManageOperation
{
    List,
    Search,
    Kill
};

struct ProcessManageRequest
{
    ProcessManageOperation operation = ProcessManageOperation::List;
    QString operationName = QStringLiteral("list");
    QString query;
    bool caseSensitive = false;
    int limit = processListDefaultLimit;
    bool includePath = false;
    bool includeArchitecture = false;
    bool hasPidFilter = false;
    int pidFilter = 0;
    int pid = 0;
    int waitMs = processKillDefaultWaitMs;
    bool force = true;
};

bool containsText(
    const QString &source,
    const QString &pattern,
    bool caseSensitive
)
{
    if ( pattern.isEmpty() )
    {
        return true;
    }
    const Qt::CaseSensitivity caseSensitivity = caseSensitive
        ? Qt::CaseSensitive
        : Qt::CaseInsensitive;
    return source.contains(pattern, caseSensitivity);
}

bool parseManageRequest(
    const QJsonValue &params,
    ProcessManageRequest *request,
    QString *error
)
{
    if ( !Common::failIfNull(request, error, QStringLiteral("process.manage internal error: request output pointer is null")) )
    {
        return false;
    }
    *request = ProcessManageRequest();
    QJsonObject paramsObject;
    if ( !Common::parseParamsObject(
            params,
            &paramsObject,
            error,
            QStringLiteral("process.manage")
        ) )
    {
        return false;
    }
    QString operation;
    if ( !Common::parseOptionalToken(
            paramsObject,
            QStringLiteral("operation"),
            QStringLiteral("list"),
            &operation,
            error,
            QStringLiteral("process.manage"),
            false
        ) )
    {
        return false;
    }
    if ( operation == QStringLiteral("list") )
    {
        request->operation = ProcessManageOperation::List;
        request->operationName = QStringLiteral("list");
    }
    else if ( operation == QStringLiteral("search") )
    {
        request->operation = ProcessManageOperation::Search;
        request->operationName = QStringLiteral("search");
    }
    else if ( operation == QStringLiteral("kill") )
    {
        request->operation = ProcessManageOperation::Kill;
        request->operationName = QStringLiteral("kill");
    }
    else
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage operation must be one of: list, search, kill");
        }
        return false;
    }

    if ( request->operation == ProcessManageOperation::Kill )
    {
        if ( !Common::parseRequiredInt(
                paramsObject,
                QStringLiteral("pid"),
                1,
                INT_MAX,
                &request->pid,
                error,
                Common::IntegerParseStyle::Number,
                QStringLiteral("process.manage")
            ) )
        {
            return false;
        }
        if ( !Common::parseOptionalInt(
                paramsObject,
                QStringLiteral("waitMs"),
                processKillMinWaitMs,
                processKillMaxWaitMs,
                processKillDefaultWaitMs,
                &request->waitMs,
                error,
                Common::IntegerParseStyle::Number,
                QStringLiteral("process.manage")
            ) )
        {
            return false;
        }
        return Common::parseOptionalBool(
            paramsObject,
            QStringLiteral("force"),
            true,
            &request->force,
            error,
            QStringLiteral("process.manage")
        );
    }

    if ( !Common::parseOptionalTrimmedStringAlias(
            paramsObject,
            QStringLiteral("query"),
            QStringLiteral("keyword"),
            &request->query,
            error,
            QStringLiteral("process.manage")
        ) )
    {
        return false;
    }

    if ( !Common::parseOptionalBool(
            paramsObject,
            QStringLiteral("caseSensitive"),
            false,
            &request->caseSensitive,
            error,
            QStringLiteral("process.manage")
        ) )
    {
        return false;
    }
    if ( !Common::parseOptionalInt(
            paramsObject,
            QStringLiteral("limit"),
            processListMinLimit,
            processListMaxLimit,
            processListDefaultLimit,
            &request->limit,
            error,
            Common::IntegerParseStyle::Number,
            QStringLiteral("process.manage")
        ) )
    {
        return false;
    }
    if ( !Common::parseOptionalBool(
            paramsObject,
            QStringLiteral("includePath"),
            false,
            &request->includePath,
            error,
            QStringLiteral("process.manage")
        ) )
    {
        return false;
    }
    if ( !Common::parseOptionalBool(
            paramsObject,
            QStringLiteral("includeArchitecture"),
            false,
            &request->includeArchitecture,
            error,
            QStringLiteral("process.manage")
        ) )
    {
        return false;
    }

    if ( !Common::parseOptionalIntWithPresence(
            paramsObject,
            QStringLiteral("pid"),
            1,
            INT_MAX,
            0,
            &request->pidFilter,
            &request->hasPidFilter,
            error,
            Common::IntegerParseStyle::Number,
            QStringLiteral("process.manage")
        ) )
    {
        return false;
    }

    if ( ( request->operation == ProcessManageOperation::Search ) &&
         request->query.isEmpty() &&
         !request->hasPidFilter )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage search requires query or pid");
        }
        return false;
    }

    return true;
}

#ifdef Q_OS_WIN

DWORD WINAPI terminateSelfProcessThreadProc(LPVOID)
{
    Sleep(static_cast<DWORD>(processKillSelfDelayMs));
    TerminateProcess(GetCurrentProcess(), 1);
    return 0;
}

bool scheduleSelfTermination(QString *error)
{
    // Detached thread: schedule process exit without blocking request handling.
    HANDLE threadHandle = CreateThread(
        nullptr,
        0,
        terminateSelfProcessThreadProc,
        nullptr,
        0,
        nullptr
    );
    if ( threadHandle == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "process.manage failed to schedule self termination: %1"
            ).arg(Common::win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    CloseHandle(threadHandle);
    return true;
}

struct GracefulTerminationContext
{
    DWORD processId = 0;
    bool signalSent = false;
};

BOOL CALLBACK sendCloseToProcessWindowProc(HWND windowHandle, LPARAM userData)
{
    if ( userData == 0 )
    {
        return FALSE;
    }

    GracefulTerminationContext *context = reinterpret_cast<GracefulTerminationContext *>(userData);
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(windowHandle, &windowProcessId);
    if ( windowProcessId != context->processId )
    {
        return TRUE;
    }

    // Only target top-level windows of the process.
    if ( GetWindow(windowHandle, GW_OWNER) != nullptr )
    {
        return TRUE;
    }

    if ( PostMessageW(windowHandle, WM_CLOSE, 0, 0) != 0 )
    {
        context->signalSent = true;
    }
    return TRUE;
}

bool requestGracefulTermination(DWORD processId)
{
    GracefulTerminationContext context;
    context.processId = processId;
    context.signalSent = false;
    EnumWindows(sendCloseToProcessWindowProc, reinterpret_cast<LPARAM>(&context));
    return context.signalSent;
}

QString queryProcessPath(HANDLE processHandle)
{
    if ( processHandle == nullptr )
    {
        return QString();
    }

    wchar_t pathBuffer[32768] = {0};
    DWORD pathBufferSize = static_cast<DWORD>(sizeof(pathBuffer) / sizeof(pathBuffer[0]));
    if ( QueryFullProcessImageNameW(
            processHandle,
            0,
            pathBuffer,
            &pathBufferSize
        ) == 0 )
    {
        return QString();
    }
    return QString::fromWCharArray(pathBuffer, static_cast<int>(pathBufferSize));
}

bool queryProcessIsWow64(HANDLE processHandle, bool *isWow64)
{
    if ( isWow64 == nullptr )
    {
        return false;
    }

    *isWow64 = false;
    if ( processHandle == nullptr )
    {
        return false;
    }

    BOOL wow64 = FALSE;
    if ( IsWow64Process(processHandle, &wow64) == 0 )
    {
        return false;
    }

    *isWow64 = ( wow64 != FALSE );
    return true;
}

QString waitResultName(DWORD waitResult)
{
    if ( waitResult == WAIT_OBJECT_0 )
    {
        return QStringLiteral("signaled");
    }
    if ( waitResult == WAIT_TIMEOUT )
    {
        return QStringLiteral("timeout");
    }
    if ( waitResult == WAIT_ABANDONED )
    {
        return QStringLiteral("abandoned");
    }
    if ( waitResult == WAIT_FAILED )
    {
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

bool lookupProcessNameByPid(DWORD pid, QString *name)
{
    if ( name == nullptr )
    {
        return false;
    }
    name->clear();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if ( snapshot == INVALID_HANDLE_VALUE )
    {
        return false;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);
    bool found = false;
    if ( Process32FirstW(snapshot, &entry) != 0 )
    {
        do
        {
            if ( entry.th32ProcessID == pid )
            {
                *name = QString::fromWCharArray(entry.szExeFile).trimmed();
                found = true;
                break;
            }
        }
        while ( Process32NextW(snapshot, &entry) != 0 );
    }
    CloseHandle(snapshot);
    return found;
}

bool isKnownCriticalProcessName(const QString &processName)
{
    const QString normalized = processName.trimmed().toLower();
    if ( normalized.isEmpty() )
    {
        return false;
    }

    return
        ( normalized == QStringLiteral("smss.exe") ) ||
        ( normalized == QStringLiteral("csrss.exe") ) ||
        ( normalized == QStringLiteral("wininit.exe") ) ||
        ( normalized == QStringLiteral("winlogon.exe") ) ||
        ( normalized == QStringLiteral("services.exe") ) ||
        ( normalized == QStringLiteral("lsass.exe") ) ||
        ( normalized == QStringLiteral("system") ) ||
        ( normalized == QStringLiteral("registry") );
}

bool isKnownCriticalProcessId(DWORD processId)
{
    return processId == 4;
}

bool queryProcessCriticalFlag(HANDLE processHandle, bool *isCritical)
{
    if ( isCritical == nullptr )
    {
        return false;
    }

    *isCritical = false;
    if ( processHandle == nullptr )
    {
        return false;
    }

    typedef BOOL (WINAPI *IsProcessCriticalFn)(HANDLE, PBOOL);
    HMODULE kernel32Module = GetModuleHandleW(L"kernel32.dll");
    if ( kernel32Module == nullptr )
    {
        return false;
    }

    IsProcessCriticalFn isProcessCriticalFn = reinterpret_cast<IsProcessCriticalFn>(
        GetProcAddress(kernel32Module, "IsProcessCritical")
    );
    if ( isProcessCriticalFn == nullptr )
    {
        return false;
    }

    BOOL critical = FALSE;
    if ( isProcessCriticalFn(processHandle, &critical) == 0 )
    {
        return false;
    }

    *isCritical = ( critical != FALSE );
    return true;
}

bool runListProcess(
    const ProcessManageRequest &request,
    QJsonObject *result,
    QString *error
)
{
    if ( !Common::failIfNull(result, error, QStringLiteral("process.manage internal error: output pointer is null")) )
    {
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if ( snapshot == INVALID_HANDLE_VALUE )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage failed to enumerate process snapshot: %1")
                .arg(Common::win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);
    if ( Process32FirstW(snapshot, &entry) == 0 )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage failed to read process snapshot: %1")
                .arg(Common::win32ErrorMessage(GetLastError()));
        }
        CloseHandle(snapshot);
        return false;
    }

    int totalMatched = 0;
    int returnedCount = 0;
    QJsonArray processes;
    do
    {
        const DWORD processId = entry.th32ProcessID;
        if ( processId == 0 )
        {
            continue;
        }
        if ( request.hasPidFilter &&
             ( static_cast<DWORD>(request.pidFilter) != processId ) )
        {
            continue;
        }

        const QString processName = QString::fromWCharArray(entry.szExeFile).trimmed();
        QString processPath;
        bool hasIsWow64 = false;
        bool isWow64 = false;
        if ( request.includePath || request.includeArchitecture )
        {
            HANDLE processHandle = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                processId
            );
            if ( processHandle != nullptr )
            {
                if ( request.includePath )
                {
                    processPath = queryProcessPath(processHandle);
                }
                if ( request.includeArchitecture )
                {
                    hasIsWow64 = queryProcessIsWow64(processHandle, &isWow64);
                }
                CloseHandle(processHandle);
            }
        }

        bool queryMatched = true;
        if ( !request.query.isEmpty() )
        {
            queryMatched =
                containsText(processName, request.query, request.caseSensitive) ||
                containsText(
                    QString::number(static_cast<qulonglong>(processId)),
                    request.query,
                    request.caseSensitive
                );
            if ( !queryMatched &&
                 request.includePath &&
                 !processPath.isEmpty() )
            {
                queryMatched = containsText(
                    processPath,
                    request.query,
                    request.caseSensitive
                );
            }
        }

        if ( !queryMatched )
        {
            continue;
        }

        ++totalMatched;
        if ( returnedCount >= request.limit )
        {
            continue;
        }

        QJsonObject processObject;
        processObject.insert(
            QStringLiteral("pid"),
            static_cast<int>(processId)
        );
        processObject.insert(
            QStringLiteral("name"),
            processName.isEmpty() ? QStringLiteral("<unknown>") : processName
        );

        DWORD sessionId = 0;
        if ( ProcessIdToSessionId(processId, &sessionId) != 0 )
        {
            processObject.insert(
                QStringLiteral("sessionId"),
                static_cast<int>(sessionId)
            );
        }
        if ( request.includePath && !processPath.isEmpty() )
        {
            processObject.insert(QStringLiteral("path"), processPath);
        }
        if ( request.includeArchitecture && hasIsWow64 )
        {
            processObject.insert(QStringLiteral("isWow64"), isWow64);
        }

        processes.append(processObject);
        ++returnedCount;
    }
    while ( Process32NextW(snapshot, &entry) != 0 );

    CloseHandle(snapshot);

    QJsonObject out;
    out.insert(QStringLiteral("operation"), request.operationName);
    if ( !request.query.isEmpty() )
    {
        out.insert(QStringLiteral("query"), request.query);
    }
    if ( request.hasPidFilter )
    {
        out.insert(QStringLiteral("pid"), request.pidFilter);
    }
    out.insert(QStringLiteral("caseSensitive"), request.caseSensitive);
    out.insert(QStringLiteral("limit"), request.limit);
    out.insert(QStringLiteral("includePath"), request.includePath);
    out.insert(QStringLiteral("includeArchitecture"), request.includeArchitecture);
    out.insert(QStringLiteral("returnedCount"), returnedCount);
    out.insert(QStringLiteral("totalMatched"), totalMatched);
    out.insert(QStringLiteral("truncated"), totalMatched > returnedCount);
    out.insert(QStringLiteral("processes"), processes);

    *result = out;
    return true;
}

bool runKillProcess(
    const ProcessManageRequest &request,
    QJsonObject *result,
    QString *error
)
{
    if ( !Common::failIfNull(result, error, QStringLiteral("process.manage internal error: output pointer is null")) )
    {
        return false;
    }

    const DWORD processId = static_cast<DWORD>(request.pid);
    const DWORD currentProcessId = GetCurrentProcessId();
    DWORD processAccess = PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
    if ( request.force || ( processId == currentProcessId ) )
    {
        processAccess |= PROCESS_TERMINATE;
    }
    HANDLE processHandle = OpenProcess(
        processAccess,
        FALSE,
        processId
    );
    if ( processHandle == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage failed to open process pid=%1: %2")
                .arg(QString::number(request.pid), Common::win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    QString processName;
    lookupProcessNameByPid(processId, &processName);
    const QString processPath = queryProcessPath(processHandle);
    bool hasIsWow64 = false;
    bool isWow64 = false;
    hasIsWow64 = queryProcessIsWow64(processHandle, &isWow64);

    if ( processId == currentProcessId )
    {
        if ( !request.force )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("process.manage self kill requires force=true");
            }
            CloseHandle(processHandle);
            return false;
        }

        // Special case: killing the node itself. Return normalized kill result first,
        // then terminate after a short internal delay so caller can receive JSON payload.
        if ( !scheduleSelfTermination(error) )
        {
            CloseHandle(processHandle);
            return false;
        }

        QJsonObject out;
        out.insert(QStringLiteral("operation"), request.operationName);
        out.insert(QStringLiteral("pid"), request.pid);
        if ( !processName.isEmpty() )
        {
            out.insert(QStringLiteral("name"), processName);
        }
        if ( !processPath.isEmpty() )
        {
            out.insert(QStringLiteral("path"), processPath);
        }
        if ( hasIsWow64 )
        {
            out.insert(QStringLiteral("isWow64"), isWow64);
        }
        out.insert(QStringLiteral("force"), request.force);
        out.insert(QStringLiteral("waitMs"), request.waitMs);
        out.insert(QStringLiteral("waitResult"), QStringLiteral("timeout"));
        out.insert(QStringLiteral("terminated"), true);
        out.insert(QStringLiteral("exited"), false);
        out.insert(QStringLiteral("resultClass"), QStringLiteral("termination_signaled"));

        CloseHandle(processHandle);
        *result = out;
        return true;
    }

    // For non-self targets, enforce critical-process protection.
    bool isCriticalProcess = false;
    const bool criticalQueryOk = queryProcessCriticalFlag(
        processHandle,
        &isCriticalProcess
    );
    const QString processNameForJudge = !processName.isEmpty()
        ? processName
        : QFileInfo(processPath).fileName();
    if ( isCriticalProcess ||
         ( !criticalQueryOk &&
           ( isKnownCriticalProcessId(processId) ||
             isKnownCriticalProcessName(processNameForJudge) ) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "process.manage refuses to terminate critical process pid=%1 name=%2"
            ).arg(
                QString::number(request.pid),
                processNameForJudge.isEmpty()
                    ? QStringLiteral("<unknown>")
                    : processNameForJudge
            );
        }
        CloseHandle(processHandle);
        return false;
    }

    if ( request.force )
    {
        if ( TerminateProcess(processHandle, 1) == 0 )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("process.manage failed to terminate process pid=%1: %2")
                    .arg(QString::number(request.pid), Common::win32ErrorMessage(GetLastError()));
            }
            CloseHandle(processHandle);
            return false;
        }
    }
    else if ( !requestGracefulTermination(processId) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "process.manage failed to request graceful termination pid=%1 (try force=true)"
            ).arg(QString::number(request.pid));
        }
        CloseHandle(processHandle);
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(
        processHandle,
        static_cast<DWORD>(request.waitMs)
    );
    if ( waitResult == WAIT_FAILED )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage failed while waiting process pid=%1: %2")
                .arg(QString::number(request.pid), Common::win32ErrorMessage(GetLastError()));
        }
        CloseHandle(processHandle);
        return false;
    }

    DWORD exitCode = 0;
    const bool hasExitCode = ( GetExitCodeProcess(processHandle, &exitCode) != 0 );
    CloseHandle(processHandle);

    QJsonObject out;
    out.insert(QStringLiteral("operation"), request.operationName);
    out.insert(QStringLiteral("pid"), request.pid);
    if ( !processName.isEmpty() )
    {
        out.insert(QStringLiteral("name"), processName);
    }
    if ( !processPath.isEmpty() )
    {
        out.insert(QStringLiteral("path"), processPath);
    }
    if ( hasIsWow64 )
    {
        out.insert(QStringLiteral("isWow64"), isWow64);
    }
    out.insert(QStringLiteral("force"), request.force);
    out.insert(QStringLiteral("waitMs"), request.waitMs);
    out.insert(QStringLiteral("waitResult"), waitResultName(waitResult));
    out.insert(QStringLiteral("terminated"), true);
    out.insert(QStringLiteral("exited"), waitResult == WAIT_OBJECT_0);
    out.insert(
        QStringLiteral("resultClass"),
        waitResult == WAIT_OBJECT_0
            ? QStringLiteral("terminated")
            : QStringLiteral("termination_signaled")
    );
    if ( hasExitCode )
    {
        out.insert(QStringLiteral("exitCode"), static_cast<int>(exitCode));
    }

    *result = out;
    return true;
}

#endif // Q_OS_WIN
}

bool ProcessManage::execute(
    const QJsonValue &params,
    QJsonObject *result,
    QString *error,
    bool *invalidParams
)
{
    Common::resetInvalidParams(invalidParams);
    if ( !Common::failIfNull(result, error, QStringLiteral("process.manage output pointer is null")) )
    {
        return false;
    }

    ProcessManageRequest request;
    QString parseError;
    if ( !parseManageRequest(params, &request, &parseError) )
    {
        return Common::failInvalidParams(invalidParams, error, parseError);
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.process.manage] start operation=%1"
    ).arg(request.operationName);

#ifdef Q_OS_WIN
    bool ok = false;
    if ( request.operation == ProcessManageOperation::Kill )
    {
        ok = runKillProcess(request, result, error);
    }
    else
    {
        ok = runListProcess(request, result, error);
    }
    if ( ok )
    {
        qInfo().noquote() << QStringLiteral(
            "[capability.process.manage] done operation=%1"
        ).arg(request.operationName);
    }
    return ok;
#else
    Q_UNUSED(request)
    if ( error != nullptr )
    {
        *error = QStringLiteral("process.manage is only supported on Windows");
    }
    return false;
#endif
}

