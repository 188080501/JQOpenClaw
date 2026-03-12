#ifndef JQOPENCLAW_COMMON_COMMON_H_
#define JQOPENCLAW_COMMON_COMMON_H_

// Qt lib import
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QSet>
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

enum class IntegerParseStyle
{
    Number,
    Integer,
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

bool parseParamsObject(
    const QJsonValue &params,
    QJsonObject *paramsObject,
    QString *error,
    const QString &scope = QString()
);

bool parseOptionalString(
    const QJsonObject &paramsObject,
    const QString &field,
    QString *value,
    QString *error,
    const QString &scope = QString(),
    bool trim = false
);

bool parseRequiredString(
    const QJsonObject &paramsObject,
    const QString &field,
    QString *value,
    QString *error,
    const QString &scope = QString(),
    bool trim = false,
    bool allowEmpty = true,
    bool missingAsTypeError = false
);

bool parseOptionalStringArray(
    const QJsonObject &paramsObject,
    const QString &field,
    QStringList *out,
    QString *error,
    const QString &scope = QString()
);

bool parseRequiredStringArray(
    const QJsonObject &paramsObject,
    const QString &field,
    QStringList *out,
    QString *error,
    const QString &scope = QString()
);

bool calculateFileMd5Hex(
    const QString &path,
    QString *md5Hex,
    QString *error,
    const QString &errorScope
);

bool parseJsonObject(
    const QByteArray &jsonBytes,
    QJsonObject *object,
    QString *error
);

QString lastOpenSslError();

QString win32ErrorMessage(quint32 errorCode);

bool parseTimeoutMs(
    const QJsonObject &paramsObject,
    const QString &field,
    int defaultValue,
    int minValue,
    int maxValue,
    int *timeoutMs,
    QString *error,
    const QString &scope
);

bool parseIntValue(
    const QJsonValue &rawValue,
    const QString &field,
    int minValue,
    int maxValue,
    int *value,
    QString *error,
    IntegerParseStyle style,
    const QString &scope
);

bool parseOptionalInt(
    const QJsonObject &paramsObject,
    const QString &field,
    int minValue,
    int maxValue,
    int defaultValue,
    int *value,
    QString *error,
    IntegerParseStyle style,
    const QString &scope
);

bool parseRequiredInt(
    const QJsonObject &paramsObject,
    const QString &field,
    int minValue,
    int maxValue,
    int *value,
    QString *error,
    IntegerParseStyle style,
    const QString &scope
);

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
    QString *error,
    const QString &scope = QString()
);

bool parseProcessEnvironment(
    const QJsonObject &paramsObject,
    const QString &environmentField,
    const QString &inheritField,
    bool defaultInheritEnvironment,
    QProcessEnvironment *environment,
    QString *error,
    const QString &scope = QString(),
    bool ignorePathOverride = false,
    const QSet<QString> *ignoredUpperKeys = nullptr,
    const QString &warningPrefix = QString()
);

QString encodingName(ContentEncoding encoding);
}

#endif // JQOPENCLAW_COMMON_COMMON_H_
