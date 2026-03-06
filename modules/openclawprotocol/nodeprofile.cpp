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
    {"file", "file.write", true},
    {"process", "process.manage", true},
    {"process", "process.which", true},
    {"system", "system.run", true},
    {"system", "system.screenshot", true},
    {"system", "system.info", true},
    {"system", "system.notify", true},
    {"system", "system.clipboard", true},
    {"system", "system.input", true},
    {"node", "node.selfUpdate", true},
};

bool resolvePermission(
    const CapabilityDeclaration &declaration,
    const QJsonObject &overrides
)
{
    const QString command = QString::fromLatin1(declaration.command);
    const QJsonValue overrideValue = overrides.value(command);
    if ( overrideValue.isBool() )
    {
        return overrideValue.toBool(declaration.permissionGranted);
    }
    return declaration.permissionGranted;
}
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
#ifdef JQOPENCLAWNODE_VERSION
    return JQOPENCLAWNODE_VERSION;
#else
    return QStringLiteral("1.0.0");
#endif
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
    return permissions(QJsonObject());
}

QJsonObject NodeProfile::permissions(const QJsonObject &overrides)
{
    QJsonObject out;
    for ( const auto &declaration : capabilityDeclarations )
    {
        out.insert(
            QString::fromLatin1(declaration.command),
            resolvePermission(declaration, overrides)
        );
    }
    return out;
}

QJsonObject NodeProfile::normalizePermissions(const QJsonObject &candidate)
{
    return permissions(candidate);
}

bool NodeProfile::isKnownCommand(const QString &command)
{
    for ( const auto &declaration : capabilityDeclarations )
    {
        if ( command == QString::fromLatin1(declaration.command) )
        {
            return true;
        }
    }
    return false;
}

bool NodeProfile::isCommandEnabled(
    const QString &command,
    const QJsonObject &overrides
)
{
    for ( const auto &declaration : capabilityDeclarations )
    {
        if ( command == QString::fromLatin1(declaration.command) )
        {
            return resolvePermission(declaration, overrides);
        }
    }
    return false;
}
