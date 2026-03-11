#ifndef JQOPENCLAW_COMMON_COMMON_H_
#define JQOPENCLAW_COMMON_COMMON_H_

// Qt lib import
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace Common
{

enum class ContentEncoding
{
    Utf8,
    Base64,
};

QJsonArray toJsonArray(const QStringList &items);

QString processExitStatusName(QProcess::ExitStatus exitStatus);

QString processErrorName(QProcess::ProcessError processError);

bool hasProcessError(QProcess::ProcessError processError);

QString processResultClass(bool timedOut, QProcess::ExitStatus exitStatus, int exitCode);

Qt::CaseSensitivity pathCaseSensitivity();

QString normalizeToken(const QString &value);

QString extractStringRaw(const QJsonObject &object, const QString &key);

QString extractStringTrimmed(const QJsonObject &object, const QString &key);

bool calculateFileMd5Hex(
    const QString &path,
    QString *md5Hex,
    QString *error,
    const QString &errorScope
);

QString lastOpenSslError();

bool parseEncoding(
    const QJsonObject &paramsObject,
    const QString &field,
    ContentEncoding *encoding,
    QString *error
);

bool parseOptionalBool(
    const QJsonObject &paramsObject,
    const QString &field,
    bool defaultValue,
    bool *out,
    QString *error
);

QString encodingName(ContentEncoding encoding);
}

#endif // JQOPENCLAW_COMMON_COMMON_H_
