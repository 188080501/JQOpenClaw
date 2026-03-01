// .h include
#include "capabilities/system/systeminfo.h"

// Qt lib import
#include <QDebug>
#include <QProcess>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>

namespace {
QString runWmic(const QStringList &arguments)
{
    QProcess process;
    qInfo().noquote() << QStringLiteral("[capability.system.info] run wmic args=%1")
                             .arg(arguments.join(' '));
    process.start(QStringLiteral("wmic"), arguments);
    if ( !process.waitForFinished(3000) )
    {
        qWarning().noquote() << QStringLiteral("[capability.system.info] wmic timeout or not finished");
        return QString();
    }

    if ( process.exitStatus() != QProcess::NormalExit )
    {
        qWarning().noquote() << QStringLiteral("[capability.system.info] wmic abnormal exit");
        return QString();
    }
    if ( process.exitCode() != 0 )
    {
        const QString err = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        qWarning().noquote() << QStringLiteral("[capability.system.info] wmic exitCode=%1 stderr=%2")
                                    .arg(process.exitCode())
                                    .arg(err);
        return QString();
    }

    const QString data = QString::fromLocal8Bit(process.readAllStandardOutput()).replace('\r', "");
    const QStringList lines = data.split('\n', Qt::SkipEmptyParts);
    if ( lines.size() < 2 )
    {
        qWarning().noquote() << QStringLiteral("[capability.system.info] wmic output is empty or malformed");
        return QString();
    }

    const QString header = lines.first().trimmed();
    for ( int i = lines.size() - 1; i >= 1; --i )
    {
        const QString value = lines.at(i).trimmed();
        if ( !value.isEmpty() && value.compare(header, Qt::CaseInsensitive) != 0 )
        {
            qInfo().noquote() << QStringLiteral("[capability.system.info] wmic parsed value=%1").arg(value);
            return value;
        }
    }

    qWarning().noquote() << QStringLiteral("[capability.system.info] wmic parsed no usable value");
    return QString();
}

QString readComputerName()
{
    QString computerName = runWmic({QStringLiteral("computersystem"), QStringLiteral("get"), QStringLiteral("Name")});
    if ( !computerName.isEmpty() )
    {
        qInfo().noquote() << QStringLiteral("[capability.system.info] computerName source=wmic value=%1")
                                 .arg(computerName);
        return computerName;
    }

    computerName = QSysInfo::machineHostName().trimmed();
    if ( !computerName.isEmpty() )
    {
        qInfo().noquote() << QStringLiteral("[capability.system.info] computerName source=machineHostName value=%1")
                                 .arg(computerName);
        return computerName;
    }

    computerName = qEnvironmentVariable("COMPUTERNAME").trimmed();
    qInfo().noquote() << QStringLiteral("[capability.system.info] computerName source=env.COMPUTERNAME value=%1")
                             .arg(computerName);
    return computerName;
}

QString readCpuName()
{
    QString cpuName = runWmic({QStringLiteral("cpu"), QStringLiteral("get"), QStringLiteral("Name")});
    if ( !cpuName.isEmpty() )
    {
        qInfo().noquote() << QStringLiteral("[capability.system.info] cpuName source=wmic value=%1")
                                 .arg(cpuName);
        return cpuName;
    }

    cpuName = QSysInfo::currentCpuArchitecture().trimmed();
    qInfo().noquote() << QStringLiteral("[capability.system.info] cpuName source=cpuArchitecture value=%1")
                             .arg(cpuName);
    return cpuName;
}
}

bool SystemInfo::collect(QJsonObject *info, QString *error)
{
    qInfo().noquote() << QStringLiteral("[capability.system.info] collect start");
    if ( info == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system info output pointer is null");
        }
        qWarning().noquote() << QStringLiteral("[capability.system.info] collect failed: output pointer is null");
        return false;
    }

    QJsonObject out;
    out.insert(QStringLiteral("cpuName"), readCpuName());
    out.insert(QStringLiteral("computerName"), readComputerName());
    *info = out;
    qInfo().noquote() << QStringLiteral("[capability.system.info] collect done cpuName=%1 computerName=%2")
                             .arg(out.value(QStringLiteral("cpuName")).toString())
                             .arg(out.value(QStringLiteral("computerName")).toString());
    return true;
}

