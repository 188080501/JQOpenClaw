// .h include
#include "capabilities/system/systeminfo.h"

// Qt lib import
#include <QAbstractSocket>
#include <QDebug>
#include <QJsonArray>
#include <QMap>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QSysInfo>
#include <QThread>
#include <QtGlobal>

namespace
{
QString runWmicRaw(const QStringList &arguments)
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

    return QString::fromLocal8Bit(process.readAllStandardOutput()).replace('\r', "");
}

QString runWmicSingleValue(const QStringList &arguments)
{
    const QString data = runWmicRaw(arguments);
    if ( data.isEmpty() )
    {
        return QString();
    }

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
        if ( !value.isEmpty() && ( value.compare(header, Qt::CaseInsensitive) != 0 ) )
        {
            qInfo().noquote() << QStringLiteral("[capability.system.info] wmic parsed value=%1").arg(value);
            return value;
        }
    }

    qWarning().noquote() << QStringLiteral("[capability.system.info] wmic parsed no usable value");
    return QString();
}

QList<QMap<QString, QString>> parseWmicValueRecords(const QString &rawOutput)
{
    QList<QMap<QString, QString>> records;
    QMap<QString, QString> current;
    const QStringList lines = rawOutput.split('\n');
    for ( const QString &line : lines )
    {
        const QString normalized = line.trimmed();
        if ( normalized.isEmpty() )
        {
            if ( !current.isEmpty() )
            {
                records.append(current);
                current.clear();
            }
            continue;
        }

        const int separatorIndex = normalized.indexOf('=');
        if ( separatorIndex <= 0 )
        {
            continue;
        }

        const QString key = normalized.left(separatorIndex).trimmed();
        const QString value = normalized.mid(separatorIndex + 1).trimmed();
        if ( !key.isEmpty() )
        {
            current.insert(key, value);
        }
    }

    if ( !current.isEmpty() )
    {
        records.append(current);
    }
    return records;
}

QList<QMap<QString, QString>> runWmicValueRecords(const QStringList &arguments)
{
    const QString rawOutput = runWmicRaw(arguments);
    if ( rawOutput.isEmpty() )
    {
        return {};
    }
    return parseWmicValueRecords(rawOutput);
}

double roundGb(double value)
{
    if ( value < 0.0 )
    {
        return 0.0;
    }
    return static_cast<double>(static_cast<qint64>(value * 100.0 + 0.5)) / 100.0;
}

double bytesToGb(quint64 bytes)
{
    return roundGb(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

qint64 bytesToRoundedGbInt(quint64 bytes)
{
    return static_cast<qint64>(
        static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) + 0.5
    );
}

double kibToGb(quint64 kib)
{
    return roundGb(static_cast<double>(kib) / (1024.0 * 1024.0));
}

QString readComputerName()
{
    QString computerName = runWmicSingleValue(
        {QStringLiteral("computersystem"), QStringLiteral("get"), QStringLiteral("Name")}
    );
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
    QString cpuName = runWmicSingleValue(
        {QStringLiteral("cpu"), QStringLiteral("get"), QStringLiteral("Name")}
    );
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

void readCpuCoreAndThreadCount(int *coreCount, int *threadCount)
{
    if ( coreCount != nullptr )
    {
        *coreCount = 0;
    }
    if ( threadCount != nullptr )
    {
        *threadCount = 0;
    }

    int parsedCoreCount = 0;
    int parsedThreadCount = 0;
    const auto records = runWmicValueRecords(
        {
            QStringLiteral("cpu"),
            QStringLiteral("get"),
            QStringLiteral("NumberOfCores,NumberOfLogicalProcessors"),
            QStringLiteral("/value")
        }
    );

    for ( const auto &record : records )
    {
        bool coreOk = false;
        const int cores = record.value(QStringLiteral("NumberOfCores")).trimmed().toInt(&coreOk);
        if ( coreOk && ( cores > 0 ) )
        {
            parsedCoreCount += cores;
        }

        bool threadOk = false;
        const int threads = record.value(QStringLiteral("NumberOfLogicalProcessors")).trimmed().toInt(&threadOk);
        if ( threadOk && ( threads > 0 ) )
        {
            parsedThreadCount += threads;
        }
    }

    if ( parsedThreadCount <= 0 )
    {
        const int idealThreadCount = QThread::idealThreadCount();
        if ( idealThreadCount > 0 )
        {
            parsedThreadCount = idealThreadCount;
        }
    }

    if ( coreCount != nullptr )
    {
        *coreCount = parsedCoreCount;
    }
    if ( threadCount != nullptr )
    {
        *threadCount = parsedThreadCount;
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.system.info] cpu topology parsed cores=%1 threads=%2"
    ).arg(parsedCoreCount).arg(parsedThreadCount);
}

QJsonObject readMemoryInfo()
{
    QJsonObject memory;

    quint64 totalPhysicalBytes = 0;
    bool hasTotalPhysicalBytes = false;
    const auto computerSystemRecords = runWmicValueRecords(
        {
            QStringLiteral("computersystem"),
            QStringLiteral("get"),
            QStringLiteral("TotalPhysicalMemory"),
            QStringLiteral("/value")
        }
    );
    for ( const auto &record : computerSystemRecords )
    {
        const QString value = record.value(QStringLiteral("TotalPhysicalMemory")).trimmed();
        bool ok = false;
        const quint64 parsed = value.toULongLong(&ok);
        if ( ok && ( parsed > 0 ) )
        {
            totalPhysicalBytes = parsed;
            hasTotalPhysicalBytes = true;
            break;
        }
    }

    quint64 totalVisibleKib = 0;
    bool hasTotalVisibleKib = false;
    quint64 freePhysicalKib = 0;
    bool hasFreePhysicalKib = false;
    const auto osRecords = runWmicValueRecords(
        {
            QStringLiteral("os"),
            QStringLiteral("get"),
            QStringLiteral("TotalVisibleMemorySize,FreePhysicalMemory"),
            QStringLiteral("/value")
        }
    );
    for ( const auto &record : osRecords )
    {
        if ( !hasTotalVisibleKib )
        {
            const QString totalVisible = record.value(QStringLiteral("TotalVisibleMemorySize")).trimmed();
            bool ok = false;
            const quint64 parsed = totalVisible.toULongLong(&ok);
            if ( ok && ( parsed > 0 ) )
            {
                totalVisibleKib = parsed;
                hasTotalVisibleKib = true;
            }
        }

        if ( !hasFreePhysicalKib )
        {
            const QString freePhysical = record.value(QStringLiteral("FreePhysicalMemory")).trimmed();
            bool ok = false;
            const quint64 parsed = freePhysical.toULongLong(&ok);
            if ( ok && ( parsed > 0 ) )
            {
                freePhysicalKib = parsed;
                hasFreePhysicalKib = true;
            }
        }
    }

    if ( hasTotalPhysicalBytes )
    {
        memory.insert(QStringLiteral("totalGB"), bytesToGb(totalPhysicalBytes));
    }
    else if ( hasTotalVisibleKib )
    {
        memory.insert(QStringLiteral("totalGB"), kibToGb(totalVisibleKib));
    }

    if ( hasTotalVisibleKib && hasFreePhysicalKib && ( totalVisibleKib >= freePhysicalKib ) )
    {
        memory.insert(QStringLiteral("usedGB"), kibToGb(totalVisibleKib - freePhysicalKib));
    }
    else if ( hasTotalPhysicalBytes && hasFreePhysicalKib )
    {
        const quint64 totalPhysicalKib = totalPhysicalBytes / 1024;
        if ( totalPhysicalKib >= freePhysicalKib )
        {
            memory.insert(QStringLiteral("usedGB"), kibToGb(totalPhysicalKib - freePhysicalKib));
        }
    }

    qInfo().noquote() << QStringLiteral(
        "[capability.system.info] memory parsed totalGB=%1 usedGB=%2"
    ).arg(
        QString::number(memory.value(QStringLiteral("totalGB")).toDouble(), 'f', 2),
        QString::number(memory.value(QStringLiteral("usedGB")).toDouble(), 'f', 2)
    );
    return memory;
}

QJsonArray readGpuNames()
{
    QSet<QString> uniqueNames;
    const auto records = runWmicValueRecords(
        {
            QStringLiteral("path"),
            QStringLiteral("win32_VideoController"),
            QStringLiteral("get"),
            QStringLiteral("Name"),
            QStringLiteral("/value")
        }
    );
    for ( const auto &record : records )
    {
        const QString name = record.value(QStringLiteral("Name")).trimmed();
        if ( !name.isEmpty() )
        {
            uniqueNames.insert(name);
        }
    }

    if ( uniqueNames.isEmpty() )
    {
        const QString singleName = runWmicSingleValue(
            {
                QStringLiteral("path"),
                QStringLiteral("win32_VideoController"),
                QStringLiteral("get"),
                QStringLiteral("Name")
            }
        ).trimmed();
        if ( !singleName.isEmpty() )
        {
            uniqueNames.insert(singleName);
        }
    }

    QJsonArray gpus;
    for ( const QString &name : uniqueNames )
    {
        gpus.append(name);
    }
    qInfo().noquote() << QStringLiteral("[capability.system.info] gpu parsed count=%1")
                             .arg(gpus.size());
    return gpus;
}

QJsonObject readIpInfo()
{
    QSet<QString> ipv4Set;
    QSet<QString> ipv6Set;

    const auto interfaces = QNetworkInterface::allInterfaces();
    for ( const QNetworkInterface &interface : interfaces )
    {
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        if ( !flags.testFlag(QNetworkInterface::IsUp) ||
             !flags.testFlag(QNetworkInterface::IsRunning) ||
             flags.testFlag(QNetworkInterface::IsLoopBack) )
        {
            continue;
        }

        const auto entries = interface.addressEntries();
        for ( const QNetworkAddressEntry &entry : entries )
        {
            const QHostAddress address = entry.ip();
            if ( address.isNull() || address.isLoopback() )
            {
                continue;
            }

            if ( address.protocol() == QAbstractSocket::IPv4Protocol )
            {
                const QString ip = address.toString().trimmed();
                if ( !ip.isEmpty() )
                {
                    ipv4Set.insert(ip);
                }
                continue;
            }

            if ( ( address.protocol() == QAbstractSocket::IPv6Protocol ) &&
                 !address.isLinkLocal() )
            {
                QString ip = address.toString().trimmed();
                const int scopeIndex = ip.indexOf('%');
                if ( scopeIndex > 0 )
                {
                    ip = ip.left(scopeIndex);
                }
                if ( !ip.isEmpty() )
                {
                    ipv6Set.insert(ip);
                }
            }
        }
    }

    QJsonArray ipv4;
    for ( const QString &ip : ipv4Set )
    {
        ipv4.append(ip);
    }

    QJsonArray ipv6;
    for ( const QString &ip : ipv6Set )
    {
        ipv6.append(ip);
    }

    QJsonObject ipInfo;
    ipInfo.insert(QStringLiteral("ipv4"), ipv4);
    ipInfo.insert(QStringLiteral("ipv6"), ipv6);
    qInfo().noquote() << QStringLiteral(
        "[capability.system.info] ip parsed ipv4=%1 ipv6=%2"
    ).arg(ipv4.size()).arg(ipv6.size());
    return ipInfo;
}

QJsonArray readDiskInfo()
{
    QJsonArray disks;
    const auto records = runWmicValueRecords(
        {
            QStringLiteral("diskdrive"),
            QStringLiteral("get"),
            QStringLiteral("Model,Size"),
            QStringLiteral("/value")
        }
    );
    for ( const auto &record : records )
    {
        const QString model = record.value(QStringLiteral("Model")).trimmed();
        const QString sizeText = record.value(QStringLiteral("Size")).trimmed();
        if ( model.isEmpty() && sizeText.isEmpty() )
        {
            continue;
        }

        QJsonObject disk;
        disk.insert(
            QStringLiteral("name"),
            model.isEmpty() ? QStringLiteral("Unknown Disk") : model
        );

        bool sizeOk = false;
        const quint64 sizeBytes = sizeText.toULongLong(&sizeOk);
        if ( sizeOk && ( sizeBytes > 0 ) )
        {
            disk.insert(QStringLiteral("capacityGB"), bytesToRoundedGbInt(sizeBytes));
        }

        disks.append(disk);
    }
    qInfo().noquote() << QStringLiteral("[capability.system.info] disk parsed count=%1")
                             .arg(disks.size());
    return disks;
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
    int cpuCores = 0;
    int cpuThreads = 0;
    readCpuCoreAndThreadCount(&cpuCores, &cpuThreads);
    if ( cpuCores > 0 )
    {
        out.insert(QStringLiteral("cpuCores"), cpuCores);
    }
    if ( cpuThreads > 0 )
    {
        out.insert(QStringLiteral("cpuThreads"), cpuThreads);
    }
    out.insert(QStringLiteral("computerName"), readComputerName());
    out.insert(QStringLiteral("memory"), readMemoryInfo());
    out.insert(QStringLiteral("gpuNames"), readGpuNames());
    out.insert(QStringLiteral("ip"), readIpInfo());
    out.insert(QStringLiteral("disks"), readDiskInfo());
    *info = out;
    qInfo().noquote() << QStringLiteral(
        "[capability.system.info] collect done cpuName=%1 cpuCores=%2 cpuThreads=%3 computerName=%4 gpuCount=%5 diskCount=%6"
    )
                             .arg(out.value(QStringLiteral("cpuName")).toString())
                             .arg(out.value(QStringLiteral("cpuCores")).toInt())
                             .arg(out.value(QStringLiteral("cpuThreads")).toInt())
                             .arg(out.value(QStringLiteral("computerName")).toString())
                             .arg(out.value(QStringLiteral("gpuNames")).toArray().size())
                             .arg(out.value(QStringLiteral("disks")).toArray().size());
    return true;
}

