// C++ lib import
#include <cmath>
#include <limits>

// Qt lib import
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QLockFile>
#include <QStandardPaths>

// JQOpenClaw import
#include "nodeapplication.h"
#include "openclawprotocol/nodeoptions.h"
#include "openclawprotocol/nodeprofile.h"

namespace
{

bool checkSingletonFlag(const QString &flag)
{
    auto file = new QLockFile( QString( "%1/%2" ).arg( QStandardPaths::writableLocation( QStandardPaths::TempLocation ), flag ) );
    if ( file->tryLock() )
    {
        return true;
    }

    delete file;
    return false;
}

QString generateDefaultDisplayName()
{
    const uint suffix = QRandomGenerator::global()->bounded(10000U);
    return QStringLiteral("JQOpenClawNode-%1").arg(suffix, 4, 10, QLatin1Char('0'));
}

bool applyConfigFromFile(const QString &path, NodeOptions *options, QString *error)
{
    if ( options == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("node options pointer is null");
        }
        return false;
    }

    QFile file(path);
    if ( !file.open(QIODevice::ReadOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to open config file: %1").arg(path);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if ( ( parseError.error != QJsonParseError::NoError ) ||
         !json.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid config JSON: %1").arg(path);
        }
        return false;
    }

    const QJsonObject root = json.object();
    if ( root.value("host").isString() )
    {
        options->host = root.value("host").toString().trimmed();
    }
    if ( root.value("port").isDouble() )
    {
        const double rawPort = root.value("port").toDouble(
            std::numeric_limits<double>::quiet_NaN()
        );
        const int port = static_cast<int>(rawPort);
        if ( std::isfinite(rawPort) &&
             ( rawPort == static_cast<double>(port) ) &&
             ( port > 0 ) &&
             ( port <= 65535 ) )
        {
            options->port = static_cast<quint16>(port);
        }
        else
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("invalid config port: %1").arg(path);
            }
            return false;
        }
    }
    else if ( root.contains("port") )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid config port type: %1").arg(path);
        }
        return false;
    }
    if ( root.value("token").isString() )
    {
        options->token = root.value("token").toString();
    }
    if ( root.value("tls").isBool() )
    {
        options->tls = root.value("tls").toBool(false);
    }
    if ( root.value("tlsFingerprint").isString() )
    {
        options->tlsFingerprint = root.value("tlsFingerprint").toString();
    }
    if ( root.value("displayName").isString() )
    {
        options->displayName = root.value("displayName").toString().trimmed();
    }
    if ( root.value("nodeId").isString() )
    {
        options->nodeId = root.value("nodeId").toString();
    }
    if ( root.value("identityPath").isString() )
    {
        options->identityPath = root.value("identityPath").toString();
    }
    if ( root.value("fileServerUri").isString() )
    {
        options->fileServerUri = root.value("fileServerUri").toString().trimmed();
    }
    if ( root.value("fileServerToken").isString() )
    {
        options->fileServerToken = root.value("fileServerToken").toString();
    }
    if ( root.value("deviceFamily").isString() )
    {
        options->deviceFamily = root.value("deviceFamily").toString();
    }
    if ( root.value("exitAfterRegister").isBool() )
    {
        options->exitAfterRegister = root.value("exitAfterRegister").toBool(false);
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    qSetMessagePattern( "%{time hh:mm:ss.zzz}: %{message}" );

    QGuiApplication app(argc, argv);
    app.setApplicationName("JQOpenClawNode");
    app.setApplicationVersion(NodeProfile::clientVersion());
    app.setOrganizationName("JQOpenClaw");

    if ( !checkSingletonFlag( "8a6f4ab6-68d7-4a09-9e89-0e651f573b69" ) )
    {
        qInfo().noquote() << "another instance is already running";
        return -1;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("JQOpenClaw headless node");
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hostOption("host", "Gateway host (required)", "ip-or-host");
    const QCommandLineOption portOption("port", "Gateway port (required)", "port");
    const QCommandLineOption tokenOption("token", "Gateway shared token (required)", "gateway-token");
    const QCommandLineOption tlsOption("tls", "Enable TLS (wss)");
    const QCommandLineOption tlsFingerprintOption(
        "tls-fingerprint",
        "Expected SHA-256 fingerprint for gateway certificate",
        "sha256"
    );
    const QCommandLineOption displayNameOption(
        "display-name",
        "Node display name (full name, e.g. JQOpenClawNode-1234)",
        "name"
    );
    const QCommandLineOption nodeIdOption("node-id", "Node instance id", "id");
    const QCommandLineOption configOption("config", "JSON config file path", "path");
    const QCommandLineOption identityPathOption(
        "identity-path",
        "Device identity file path",
        "path"
    );
    const QCommandLineOption fileServerUriOption(
        "file-server-uri",
        "File server base URI for screenshot upload",
        "uri"
    );
    const QCommandLineOption fileServerTokenOption(
        "file-server-token",
        "File server token sent in X-Token header",
        "token"
    );
    const QCommandLineOption deviceFamilyOption(
        "device-family",
        "Device family for auth metadata",
        "family"
    );
    const QCommandLineOption exitAfterRegisterOption(
        "exit-after-register",
        "Exit process after registration is successful"
    );

    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(tokenOption);
    parser.addOption(tlsOption);
    parser.addOption(tlsFingerprintOption);
    parser.addOption(displayNameOption);
    parser.addOption(nodeIdOption);
    parser.addOption(configOption);
    parser.addOption(identityPathOption);
    parser.addOption(fileServerUriOption);
    parser.addOption(fileServerTokenOption);
    parser.addOption(deviceFamilyOption);
    parser.addOption(exitAfterRegisterOption);

    parser.process(app);

    NodeOptions options;
    if ( parser.isSet(configOption) )
    {
        options.configPath = parser.value(configOption).trimmed();
        if ( !options.configPath.isEmpty() )
        {
            QString configError;
            if ( !applyConfigFromFile(options.configPath, &options, &configError) )
            {
                qCritical().noquote() << configError;
                return 1;
            }
        }
    }

    if ( parser.isSet(hostOption) )
    {
        options.host = parser.value(hostOption).trimmed();
    }

    if ( parser.isSet(portOption) )
    {
        bool ok = false;
        const int port = parser.value(portOption).toInt(&ok);
        if ( ok &&
             ( port > 0 ) &&
             ( port <= 65535 ) )
        {
            options.port = static_cast<quint16>(port);
        }
        else
        {
            qCritical().noquote() << "invalid --port value";
            return 1;
        }
    }

    if ( parser.isSet(tokenOption) )
    {
        options.token = parser.value(tokenOption);
    }

    if ( parser.isSet(tlsOption) )
    {
        options.tls = true;
    }

    if ( parser.isSet(tlsFingerprintOption) )
    {
        options.tlsFingerprint = parser.value(tlsFingerprintOption).trimmed();
    }

    if ( parser.isSet(displayNameOption) )
    {
        options.displayName = parser.value(displayNameOption).trimmed();
    }

    if ( parser.isSet(nodeIdOption) )
    {
        options.nodeId = parser.value(nodeIdOption);
    }

    if ( parser.isSet(identityPathOption) )
    {
        options.identityPath = parser.value(identityPathOption);
    }

    if ( parser.isSet(fileServerUriOption) )
    {
        options.fileServerUri = parser.value(fileServerUriOption).trimmed();
    }

    if ( parser.isSet(fileServerTokenOption) )
    {
        options.fileServerToken = parser.value(fileServerTokenOption);
    }

    if ( parser.isSet(deviceFamilyOption) )
    {
        options.deviceFamily = parser.value(deviceFamilyOption);
    }

    if ( parser.isSet(exitAfterRegisterOption) )
    {
        options.exitAfterRegister = true;
    }

    if ( options.displayName.trimmed().isEmpty() )
    {
        options.displayName = generateDefaultDisplayName();
    }

    if ( options.host.trimmed().isEmpty() )
    {
        qCritical().noquote() << "gateway host is empty";
        return 1;
    }

    if ( options.port == 0 )
    {
        qCritical().noquote() << "gateway port is empty";
        return 1;
    }

    if ( options.token.trimmed().isEmpty() )
    {
        qCritical().noquote() << "gateway token is empty";
        return 1;
    }

    if ( ( options.tlsFingerprint.trimmed().isEmpty() == false ) &&
         !options.tls )
    {
        qCritical().noquote() << "--tls-fingerprint requires --tls";
        return 1;
    }

    if ( !options.fileServerUri.trimmed().isEmpty() )
    {
        const QUrl fileServerUrl(options.fileServerUri.trimmed());
        if ( !fileServerUrl.isValid() ||
             fileServerUrl.scheme().trimmed().isEmpty() ||
             fileServerUrl.host().trimmed().isEmpty() )
        {
            qCritical().noquote() << "invalid --file-server-uri value";
            return 1;
        }
    }

    if ( options.fileServerUri.trimmed().isEmpty() &&
         !options.fileServerToken.trimmed().isEmpty() )
    {
        qCritical().noquote() << "--file-server-token requires --file-server-uri";
        return 1;
    }

    NodeApplication nodeApplication(options);
    QObject::connect(
        &nodeApplication,
        &NodeApplication::finished,
        &app,
        [&app](int exitCode)
        {
            app.exit(exitCode);
        }
    );

    QTimer::singleShot(0, &nodeApplication, &NodeApplication::start);
    return app.exec();
}
