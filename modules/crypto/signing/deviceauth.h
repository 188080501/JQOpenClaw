#ifndef JQOPENCLAW_CRYPTO_SIGNING_DEVICEAUTH_H_
#define JQOPENCLAW_CRYPTO_SIGNING_DEVICEAUTH_H_

// Qt lib import
#include <QByteArray>
#include <QString>
#include <QStringList>

struct DeviceAuthPayloadInput
{
    QString deviceId;
    QString clientId;
    QString clientMode;
    QString role;
    QStringList scopes;
    qint64 signedAtMs = 0;
    QString token;
    QString nonce;
    QString platform;
    QString deviceFamily;
};

namespace DeviceAuthConstants
{
inline constexpr int ed25519PublicKeyBytes = 32;
inline constexpr int ed25519SecretKeySeedBytes = 32;
inline constexpr int ed25519SecretKeyLegacyBytes = 64;
inline constexpr int ed25519SignatureBytes = 64;
}

class DeviceAuth
{
public:
    static bool initialize(QString *error);
    static QString buildPayloadV3(const DeviceAuthPayloadInput &input);
    static bool normalizeSecretKeySeed(
        const QByteArray &secretKey,
        QByteArray *secretKeySeed,
        QString *error
    );
    static bool signDetached(
        const QByteArray &secretKey,
        const QByteArray &payload,
        QString *signatureBase64Url,
        QString *error
    );
};

#endif // JQOPENCLAW_CRYPTO_SIGNING_DEVICEAUTH_H_
