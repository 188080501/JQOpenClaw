// .h include
#include "openclawprotocol/nodeprofile.h"

// Qt lib import
#include <QSet>

namespace
{
struct CapabilityDeclaration
{
    const char *capabilityCategory;
    const char *command;
    bool permissionGranted;
};

const CapabilityDeclaration capabilityDeclarations[] =
{
    {"file", "file.read", true},
    {"file", "file.write", false},
    {"process", "process.exec", true},
    {"system", "system.screenshot", true},
    {"system", "system.info", true},
};
}

int NodeProfile::minProtocolVersion()
{
    return 3;
}

int NodeProfile::maxProtocolVersion()
{
    return 3;
}

QString NodeProfile::clientId()
{
    return QStringLiteral("node-host");
}

QString NodeProfile::clientVersion()
{
    return QStringLiteral("0.1.0");
}

QJsonArray NodeProfile::caps()
{
    QSet<QString> inserted;
    QJsonArray out;
    for ( const auto &declaration : capabilityDeclarations )
    {
        const QString capabilityCategory =
            QString::fromLatin1(declaration.capabilityCategory);
        if ( inserted.contains(capabilityCategory) )
        {
            continue;
        }
        inserted.insert(capabilityCategory);
        out.append(capabilityCategory);
    }
    return out;
}

QJsonArray NodeProfile::commands()
{
    QJsonArray out;
    for ( const auto &declaration : capabilityDeclarations )
    {
        out.append(QString::fromLatin1(declaration.command));
    }
    return out;
}

QJsonObject NodeProfile::permissions()
{
    QJsonObject out;
    for ( const auto &declaration : capabilityDeclarations )
    {
        out.insert(
            QString::fromLatin1(declaration.command),
            declaration.permissionGranted
        );
    }
    return out;
}
