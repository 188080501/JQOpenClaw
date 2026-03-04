// .h include
#include "capabilities/system/systemnotify.h"

// Qt lib import
#include <QCoreApplication>
#include <QDebug>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>
#include <QWidget>

namespace
{
const int systemNotifyMessageMaxLength = 4000;
const int systemNotifyTitleMaxLength = 120;

QString defaultNotifyTitle()
{
    return QStringLiteral("JQOpenClaw");
}

void showNotifyMessageBox(
    const QString &title,
    const QString &message
)
{
    QMessageBox *messageBox = new QMessageBox(
        QMessageBox::Information,
        title,
        message,
        QMessageBox::Ok,
        nullptr
    );
    messageBox->setAttribute(Qt::WA_DeleteOnClose, true);
    messageBox->setWindowModality(Qt::NonModal);
    messageBox->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    messageBox->show();
    messageBox->raise();
    messageBox->activateWindow();
}

bool parseNotifyParams(
    const QJsonValue &params,
    QString *title,
    QString *message,
    QString *error
)
{
    if ( ( title == nullptr ) || ( message == nullptr ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify internal error: output pointer is null");
        }
        return false;
    }

    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify params must be object");
        }
        return false;
    }

    const QJsonObject paramsObject = params.toObject();

    const QJsonValue messageValue = paramsObject.value(QStringLiteral("message"));
    if ( !messageValue.isString() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify message must be string");
        }
        return false;
    }

    const QString parsedMessage = messageValue.toString();
    if ( parsedMessage.trimmed().isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify message is empty");
        }
        return false;
    }
    if ( parsedMessage.size() > systemNotifyMessageMaxLength )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify message length out of range [1, %1]")
                .arg(systemNotifyMessageMaxLength);
        }
        return false;
    }

    QString parsedTitle = defaultNotifyTitle();
    const QJsonValue titleValue = paramsObject.value(QStringLiteral("title"));
    if ( !titleValue.isUndefined() && !titleValue.isNull() )
    {
        if ( !titleValue.isString() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.notify title must be string");
            }
            return false;
        }

        const QString candidateTitle = titleValue.toString().trimmed();
        if ( !candidateTitle.isEmpty() )
        {
            parsedTitle = candidateTitle;
        }
    }

    if ( parsedTitle.size() > systemNotifyTitleMaxLength )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify title length out of range [1, %1]")
                .arg(systemNotifyTitleMaxLength);
        }
        return false;
    }

    *title = parsedTitle;
    *message = parsedMessage;
    return true;
}

bool dispatchNotifyMessageBox(
    const QString &title,
    const QString &message,
    QString *error
)
{
    QCoreApplication *application = QCoreApplication::instance();
    if ( application == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify application instance is unavailable");
        }
        return false;
    }

    if ( QThread::currentThread() == application->thread() )
    {
        showNotifyMessageBox(title, message);
        return true;
    }

    const bool invokeOk = QMetaObject::invokeMethod(
        application,
        [title, message]()
        {
            showNotifyMessageBox(title, message);
        },
        Qt::QueuedConnection
    );
    if ( !invokeOk )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.notify failed to dispatch notify dialog");
        }
        return false;
    }

    return true;
}
}

bool SystemNotify::execute(
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
            *error = QStringLiteral("system.notify output pointer is null");
        }
        return false;
    }

    QString title;
    QString message;
    QString parseError;
    if ( !parseNotifyParams(params, &title, &message, &parseError) )
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
        "[capability.system.notify] dispatch title=%1 messageLength=%2"
    ).arg(title).arg(message.size());

    QString dispatchError;
    if ( !dispatchNotifyMessageBox(title, message, &dispatchError) )
    {
        if ( error != nullptr )
        {
            *error = dispatchError.isEmpty()
                ? QStringLiteral("failed to dispatch system notify")
                : dispatchError;
        }
        return false;
    }

    QJsonObject out;
    out.insert(QStringLiteral("operation"), QStringLiteral("notify"));
    out.insert(QStringLiteral("title"), title);
    out.insert(QStringLiteral("message"), message);
    out.insert(QStringLiteral("shown"), true);
    out.insert(QStringLiteral("async"), true);
    out.insert(QStringLiteral("ok"), true);
    *result = out;

    qInfo().noquote() << QStringLiteral(
        "[capability.system.notify] dispatch done"
    );
    return true;
}

