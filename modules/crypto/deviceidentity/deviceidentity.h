#ifndef JQOPENCLAW_CRYPTO_DEVICEIDENTITY_DEVICEIDENTITY_H_
#define JQOPENCLAW_CRYPTO_DEVICEIDENTITY_DEVICEIDENTITY_H_

// Qt lib import
#include <QByteArray>
#include <QString>

struct DeviceIdentity
{
    QString deviceId;
    QByteArray publicKey;
    QByteArray secretKey;
};

class DeviceIdentityStore
{
public:
    explicit DeviceIdentityStore(const QString &identityPath = QString());

    QString identityPath() const;
    bool loadOrCreate(DeviceIdentity *identity, QString *error) const;

    static QString deriveDeviceId(const QByteArray &publicKey);

private:
    bool loadFromDisk(DeviceIdentity *identity, QString *error) const;
    bool createAndPersist(DeviceIdentity *identity, QString *error) const;
    bool persist(const DeviceIdentity &identity, QString *error) const;
    QString resolvedPath() const;

    QString identityPath_;
};

#endif // JQOPENCLAW_CRYPTO_DEVICEIDENTITY_DEVICEIDENTITY_H_
