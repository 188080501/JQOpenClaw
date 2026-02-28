#ifndef JQOPENCLAW_CAPABILITIES_SYSTEMRESOURCE_SYSTEMRESOURCEINFO_H_
#define JQOPENCLAW_CAPABILITIES_SYSTEMRESOURCE_SYSTEMRESOURCEINFO_H_

// Qt lib import
#include <QJsonObject>
#include <QString>

class SystemResourceInfo
{
public:
    static bool collect(QJsonObject *info, QString *error);
};

#endif // JQOPENCLAW_CAPABILITIES_SYSTEMRESOURCE_SYSTEMRESOURCEINFO_H_
