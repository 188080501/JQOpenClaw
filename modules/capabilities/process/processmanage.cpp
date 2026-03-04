// .h include
#include "capabilities/process/processmanage.h"

// Qt lib import
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QtGlobal>

// C++ lib import
#include <climits>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace
{
const int processListDefaultLimit = 200;
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
};

QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
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
            *error = QStringLiteral("process.manage internal error: bool output pointer is null");
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
            *error = QStringLiteral("process.manage %1 must be boolean").arg(field);
        }
        return false;
    }

    *value = rawValue.toBool();
    return true;
}

bool parseIntValue(
    const QJsonValue &rawValue,
    const QString &field,
    int minValue,
    int maxValue,
    int *value,
    QString *error
)
{
    if ( value == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage internal error: integer output pointer is null");
        }
        return false;
    }

    if ( !rawValue.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage %1 must be number").arg(field);
        }
        return false;
    }

    bool ok = false;
    const int parsedValue = QString::number(rawValue.toDouble(), 'g', 16).toInt(&ok);
    if ( !ok )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage %1 is invalid").arg(field);
        }
        return false;
    }
    if ( ( parsedValue < minValue ) || ( parsedValue > maxValue ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage %1 out of range [%2, %3]")
                .arg(field, QString::number(minValue), QString::number(maxValue));
        }
        return false;
    }

    *value = parsedValue;
    return true;
}

bool parseOptionalInt(
    const QJsonObject &paramsObject,
    const QString &field,
    int minValue,
    int maxValue,
    int defaultValue,
    int *value,
    QString *error
)
{
    if ( value == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage internal error: integer output pointer is null");
        }
        return false;
    }

    *value = defaultValue;
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        return true;
    }

    return parseIntValue(rawValue, field, minValue, maxValue, value, error);
}

bool parseRequiredInt(
    const QJsonObject &paramsObject,
    const QString &field,
    int minValue,
    int maxValue,
    int *value,
    QString *error
)
{
    const QJsonValue rawValue = paramsObject.value(field);
    if ( rawValue.isUndefined() || rawValue.isNull() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage requires %1").arg(field);
        }
        return false;
    }

    return parseIntValue(rawValue, field, minValue, maxValue, value, error);
}

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
    if ( request == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage internal error: request output pointer is null");
        }
        return false;
    }
    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage params must be object");
        }
        return false;
    }

    *request = ProcessManageRequest();
    const QJsonObject paramsObject = params.toObject();
    const QJsonValue operationValue = paramsObject.value(QStringLiteral("operation"));
    if ( operationValue.isUndefined() )
    {
        request->operation = ProcessManageOperation::List;
        request->operationName = QStringLiteral("list");
    }
    else if ( operationValue.isNull() || !operationValue.isString() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage operation must be string");
        }
        return false;
    }
    else
    {
        const QString operation = operationValue.toString().trimmed().toLower();
        if ( operation.isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("process.manage operation must not be empty");
            }
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
    }

    if ( request->operation == ProcessManageOperation::Kill )
    {
        if ( !parseRequiredInt(
                paramsObject,
                QStringLiteral("pid"),
                1,
                INT_MAX,
                &request->pid,
                error
            ) )
        {
            return false;
        }
        return parseOptionalInt(
            paramsObject,
            QStringLiteral("waitMs"),
            processKillMinWaitMs,
            processKillMaxWaitMs,
            processKillDefaultWaitMs,
            &request->waitMs,
            error
        );
    }

    request->query = extractString(paramsObject, QStringLiteral("query"));
    if ( request->query.isEmpty() )
    {
        request->query = extractString(paramsObject, QStringLiteral("keyword"));
    }

    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("caseSensitive"),
            false,
            &request->caseSensitive,
            error
        ) )
    {
        return false;
    }
    if ( !parseOptionalInt(
            paramsObject,
            QStringLiteral("limit"),
            processListMinLimit,
            processListMaxLimit,
            processListDefaultLimit,
            &request->limit,
            error
        ) )
    {
        return false;
    }
    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("includePath"),
            false,
            &request->includePath,
            error
        ) )
    {
        return false;
    }
    if ( !parseOptionalBool(
            paramsObject,
            QStringLiteral("includeArchitecture"),
            false,
            &request->includeArchitecture,
            error
        ) )
    {
        return false;
    }

    const QJsonValue pidValue = paramsObject.value(QStringLiteral("pid"));
    if ( !pidValue.isUndefined() && !pidValue.isNull() )
    {
        if ( !parseIntValue(pidValue, QStringLiteral("pid"), 1, INT_MAX, &request->pidFilter, error) )
        {
            return false;
        }
        request->hasPidFilter = true;
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

QString win32ErrorMessage(DWORD errorCode)
{
    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );
    if ( ( size == 0 ) || ( buffer == nullptr ) )
    {
        return QStringLiteral("win32 error %1").arg(QString::number(errorCode));
    }

    QString message = QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed();
    LocalFree(buffer);
    if ( message.isEmpty() )
    {
        return QStringLiteral("win32 error %1").arg(QString::number(errorCode));
    }
    return QStringLiteral("%1 (code=%2)")
        .arg(message, QString::number(errorCode));
}

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
            ).arg(win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    CloseHandle(threadHandle);
    return true;
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
    if ( result == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage internal error: output pointer is null");
        }
        return false;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if ( snapshot == INVALID_HANDLE_VALUE )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage failed to enumerate process snapshot: %1")
                .arg(win32ErrorMessage(GetLastError()));
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
                .arg(win32ErrorMessage(GetLastError()));
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
    if ( result == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage internal error: output pointer is null");
        }
        return false;
    }

    const DWORD processId = static_cast<DWORD>(request.pid);
    HANDLE processHandle = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
        FALSE,
        processId
    );
    if ( processHandle == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage failed to open process pid=%1: %2")
                .arg(QString::number(request.pid), win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    QString processName;
    lookupProcessNameByPid(processId, &processName);
    const QString processPath = queryProcessPath(processHandle);
    bool hasIsWow64 = false;
    bool isWow64 = false;
    hasIsWow64 = queryProcessIsWow64(processHandle, &isWow64);

    const DWORD currentProcessId = GetCurrentProcessId();
    if ( processId == currentProcessId )
    {
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

    if ( TerminateProcess(processHandle, 1) == 0 )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage failed to terminate process pid=%1: %2")
                .arg(QString::number(request.pid), win32ErrorMessage(GetLastError()));
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
                .arg(QString::number(request.pid), win32ErrorMessage(GetLastError()));
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
    if ( invalidParams != nullptr )
    {
        *invalidParams = false;
    }
    if ( result == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("process.manage output pointer is null");
        }
        return false;
    }

    ProcessManageRequest request;
    QString parseError;
    if ( !parseManageRequest(params, &request, &parseError) )
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
