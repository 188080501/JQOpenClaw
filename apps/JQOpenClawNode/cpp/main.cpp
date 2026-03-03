// Qt lib import
#include <QApplication>
#include <QDebug>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qqml.h>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QQuickStyle>

// JQOpenClaw import
#include "nodeapplication.h"
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
}

int main(int argc, char *argv[])
{
    qSetMessagePattern( "%{time hh:mm:ss.zzz}: %{message}" );

    QApplication app(argc, argv);
    app.setApplicationName("JQOpenClawNode");
    app.setApplicationVersion(NodeProfile::clientVersion());
    app.setOrganizationName("JQOpenClaw");
    app.setQuitOnLastWindowClosed(false);

    if ( !checkSingletonFlag( "8a6f4ab6-68d7-4a09-9e89-0e651f573b69" ) )
    {
        qInfo().noquote() << "another instance is already running";
        return -1;
    }

    QQuickStyle::setStyle( "Material" );
    qmlRegisterType< NodeApplication >( "JQOpenClawNode", 1, 0, "NodeApplication" );

    NodeApplication nodeApplication;

    QQmlApplicationEngine engine;
    engine.addImportPath( QStringLiteral( "qrc:/qml" ) );
    engine.rootContext()->setContextProperty(
        QStringLiteral("nodeApplication"),
        &nodeApplication
    );
    engine.load( QUrl( QStringLiteral( "qrc:/qml/main.qml" ) ) );
    if ( engine.rootObjects().isEmpty() )
    {
        qCritical().noquote() << "failed to load qml ui";
        return 1;
    }

    nodeApplication.setMainWindowObject(engine.rootObjects().first());
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
