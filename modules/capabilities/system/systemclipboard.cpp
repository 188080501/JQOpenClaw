// .h include
#include "capabilities/system/systemclipboard.h"

// Qt lib import
#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <QDebug>

// JQOpenClaw import
#include "common/common.h"

namespace
{
enum class ClipboardOperation
{
    Read,
    Write
};

bool parseClipboardRequest(
    const QJsonValue &params,
    ClipboardOperation *operation,
    QString *writeText,
    QString *error
)
{
    if ( !Common::failIfNull(operation, error, QStringLiteral("system.clipboard internal error: output pointer is null")) ||
         !Common::failIfNull(writeText, error, QStringLiteral("system.clipboard internal error: output pointer is null")) )
    {
        return false;
    }

    *operation = ClipboardOperation::Read;
    writeText->clear();

    QJsonObject paramsObject;
    if ( !Common::parseParamsObject(
            params,
            &paramsObject,
            error,
            QStringLiteral("system.clipboard")
        ) )
    {
        return false;
    }
    QString operationText;
    if ( !Common::parseOptionalToken(
            paramsObject,
            QStringLiteral("operation"),
            QStringLiteral("read"),
            &operationText,
            error,
            QStringLiteral("system.clipboard"),
            false
        ) )
    {
        return false;
    }

    if ( operationText == QStringLiteral("read") )
    {
        *operation = ClipboardOperation::Read;
        return true;
    }
    if ( operationText == QStringLiteral("write") )
    {
        *operation = ClipboardOperation::Write;
        QString parseError;
        if ( !Common::parseRequiredString(
                paramsObject,
                QStringLiteral("text"),
                writeText,
                &parseError,
                QString(),
                false,
                true,
                false
            ) )
        {
            if ( error != nullptr )
            {
                if ( parseError == QStringLiteral("requires text") )
                {
                    *error = QStringLiteral("system.clipboard write requires text");
                }
                else
                {
                    *error = QStringLiteral("system.clipboard %1").arg(parseError);
                }
            }
            return false;
        }
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("system.clipboard operation must be one of: read, write");
    }
    return false;
}

bool executeClipboardOperation(
    ClipboardOperation operation,
    const QString &writeText,
    QJsonObject *result,
    QString *error
)
{
    if ( !Common::failIfNull(result, error, QStringLiteral("system.clipboard result output pointer is null")) )
    {
        return false;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if ( clipboard == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.clipboard clipboard instance is unavailable");
        }
        return false;
    }

    QJsonObject out;
    if ( operation == ClipboardOperation::Read )
    {
        const QString text = clipboard->text(QClipboard::Clipboard);
        out.insert(QStringLiteral("operation"), QStringLiteral("read"));
        out.insert(QStringLiteral("text"), text);
        out.insert(QStringLiteral("length"), text.size());
        out.insert(QStringLiteral("hasText"), !text.isEmpty());
        out.insert(QStringLiteral("ok"), true);
        *result = out;
        return true;
    }

    clipboard->setText(writeText, QClipboard::Clipboard);
    out.insert(QStringLiteral("operation"), QStringLiteral("write"));
    out.insert(QStringLiteral("written"), true);
    out.insert(QStringLiteral("length"), writeText.size());
    out.insert(QStringLiteral("hasText"), !writeText.isEmpty());
    out.insert(QStringLiteral("ok"), true);
    *result = out;
    return true;
}

bool executeOnAppThread(
    ClipboardOperation operation,
    const QString &writeText,
    QJsonObject *result,
    QString *error
)
{
    QCoreApplication *application = QCoreApplication::instance();
    if ( application == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.clipboard application instance is unavailable");
        }
        return false;
    }

    if ( QThread::currentThread() == application->thread() )
    {
        return executeClipboardOperation(operation, writeText, result, error);
    }

    bool success = false;
    QString invokeError;
    QJsonObject invokeResult;
    const bool invokeOk = QMetaObject::invokeMethod(
        application,
        [&]()
        {
            success = executeClipboardOperation(operation, writeText, &invokeResult, &invokeError);
        },
        Qt::BlockingQueuedConnection
    );
    if ( !invokeOk )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.clipboard failed to dispatch clipboard operation");
        }
        return false;
    }

    if ( !success )
    {
        if ( error != nullptr )
        {
            *error = invokeError.isEmpty()
                ? QStringLiteral("system.clipboard operation failed")
                : invokeError;
        }
        return false;
    }

    if ( result != nullptr )
    {
        *result = invokeResult;
    }
    return true;
}
}

bool SystemClipboard::execute(
    const QJsonValue &params,
    QJsonObject *result,
    QString *error,
    bool *invalidParams
)
{
    Common::resetInvalidParams(invalidParams);
    if ( !Common::failIfNull(result, error, QStringLiteral("system.clipboard output pointer is null")) )
    {
        return false;
    }

    ClipboardOperation operation = ClipboardOperation::Read;
    QString writeText;
    QString parseError;
    if ( !parseClipboardRequest(params, &operation, &writeText, &parseError) )
    {
        return Common::failInvalidParams(invalidParams, error, parseError);
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.system.clipboard] start operation=%1"
    ).arg(operation == ClipboardOperation::Read ? QStringLiteral("read") : QStringLiteral("write"));

    QString executeError;
    if ( !executeOnAppThread(operation, writeText, result, &executeError) )
    {
        if ( error != nullptr )
        {
            *error = executeError.isEmpty()
                ? QStringLiteral("failed to run system clipboard")
                : executeError;
        }
        return false;
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.system.clipboard] done operation=%1"
    ).arg(operation == ClipboardOperation::Read ? QStringLiteral("read") : QStringLiteral("write"));
    return true;
}
