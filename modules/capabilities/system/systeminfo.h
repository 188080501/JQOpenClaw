#ifndef JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMINFO_H_
#define JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMINFO_H_

// Qt lib import
#include <QJsonObject>
#include <QString>

class SystemInfo
{
public:
    static bool collect(QJsonObject *info, QString *error);
};

#endif // JQOPENCLAW_CAPABILITIES_SYSTEM_SYSTEMINFO_H_
