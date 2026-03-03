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
#include <QWindow>
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

    if ( !checkSingletonFlag( "75b50ce4-915e-4834-b50c-f8249b4e1d5d" ) )
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

    QObject *mainWindowObject = engine.rootObjects().first();
    nodeApplication.setMainWindowObject(mainWindowObject);
    if ( nodeApplication.silentStartupEnabled() )
    {
        auto window = qobject_cast< QWindow * >( mainWindowObject );
        if ( window != nullptr )
        {
            window->hide();
        }
    }
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
