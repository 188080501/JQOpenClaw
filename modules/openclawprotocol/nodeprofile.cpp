// .h include
#include "openclawprotocol/nodeprofile.h"

// Qt lib import
#include <QSet>

namespace {
struct CapabilityDeclaration
{
    const char *cap;
    const char *command;
    const char *permission;
};

const CapabilityDeclaration capabilityDeclarations[] =
{
    {"screenshot", "screenshot.capture", "screenshot.capture"},
    {"system.resource", "system.resource.info", "system.resource.info"},
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
        const QString cap = QString::fromLatin1(declaration.cap);
        if ( inserted.contains(cap) )
        {
            continue;
        }
        inserted.insert(cap);
        out.append(cap);
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
        out.insert(QString::fromLatin1(declaration.permission), true);
    }
    return out;
}
