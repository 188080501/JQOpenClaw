// .h include
#include "crypto/deviceidentity/deviceidentity.h"

// Qt lib import
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

// OpenSSL lib import
#include <openssl/err.h>
#include <openssl/evp.h>

// JQOpenClaw import
#include "crypto/cryptoencoding.h"
#include "crypto/signing/deviceauth.h"

namespace
{
QString defaultIdentityPath()
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if ( basePath.isEmpty() )
    {
        basePath = QDir::homePath() + "/.jqopenclaw";
    }
    return basePath + "/identity/device.json";
}

QString lastOpenSslError()
{
    const unsigned long errorCode = ERR_get_error();
    if ( errorCode == 0UL )
    {
        return QStringLiteral("unknown OpenSSL error");
    }

    char buffer[256] = {0};
    ERR_error_string_n(errorCode, buffer, sizeof(buffer));
    return QString::fromLatin1(buffer);
}

bool generateEd25519KeyPair(
    QByteArray *publicKey,
    QByteArray *secretKeySeed,
    QString *error
)
{
    if ( ( publicKey == nullptr ) ||
         ( secretKeySeed == nullptr ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("ed25519 key output pointer is null");
        }
        return false;
    }

    EVP_PKEY_CTX *keyCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if ( keyCtx == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to create Ed25519 key context: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    if ( EVP_PKEY_keygen_init(keyCtx) != 1 )
    {
        EVP_PKEY_CTX_free(keyCtx);
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to initialize Ed25519 key generation: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    EVP_PKEY *key = nullptr;
    if ( ( EVP_PKEY_keygen(keyCtx, &key) != 1 ) ||
         ( key == nullptr ) )
    {
        EVP_PKEY_free(key);
        EVP_PKEY_CTX_free(keyCtx);
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to generate Ed25519 key pair: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    publicKey->resize(DeviceAuthConstants::ed25519PublicKeyBytes);
    size_t publicKeyLength = static_cast<size_t>(publicKey->size());
    const bool exportPublicOk = EVP_PKEY_get_raw_public_key(
        key,
        reinterpret_cast<unsigned char *>(publicKey->data()),
        &publicKeyLength
    ) == 1;

    secretKeySeed->resize(DeviceAuthConstants::ed25519SecretKeySeedBytes);
    size_t secretKeyLength = static_cast<size_t>(secretKeySeed->size());
    const bool exportPrivateOk = EVP_PKEY_get_raw_private_key(
        key,
        reinterpret_cast<unsigned char *>(secretKeySeed->data()),
        &secretKeyLength
    ) == 1;

    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(keyCtx);

    if ( !exportPublicOk ||
         ( publicKeyLength != static_cast<size_t>(DeviceAuthConstants::ed25519PublicKeyBytes) ) ||
         !exportPrivateOk ||
         ( secretKeyLength != static_cast<size_t>(DeviceAuthConstants::ed25519SecretKeySeedBytes) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to export Ed25519 key pair: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    return true;
}
}

DeviceIdentityStore::DeviceIdentityStore(const QString &identityPath) :
    identityPath_(identityPath.trimmed())
{
}

QString DeviceIdentityStore::identityPath() const
{
    return resolvedPath();
}

bool DeviceIdentityStore::loadOrCreate(DeviceIdentity *identity, QString *error) const
{
    if ( identity == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("device identity output pointer is null");
        }
        return false;
    }

    QString loadError;
    if ( loadFromDisk(identity, &loadError) )
    {
        return true;
    }

    if ( !loadError.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = loadError;
        }
        return false;
    }

    return createAndPersist(identity, error);
}

QString DeviceIdentityStore::deriveDeviceId(const QByteArray &publicKey)
{
    if ( publicKey.size() != DeviceAuthConstants::ed25519PublicKeyBytes )
    {
        return QString();
    }

    const QByteArray hash = QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

bool DeviceIdentityStore::loadFromDisk(DeviceIdentity *identity, QString *error) const
{
    const QString path = resolvedPath();
    QFile file(path);
    if ( !file.exists() )
    {
        return false;
    }

    if ( !file.open(QIODevice::ReadOnly) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to open identity file: %1").arg(path);
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
            *error = QStringLiteral("invalid identity file JSON: %1").arg(path);
        }
        return false;
    }

    const QJsonObject root = json.object();
    const int version = root.value("version").toInt(-1);
    const QString storedDeviceId = root.value("deviceId").toString();
    const QString publicKeyText = root.value("publicKey").toString();
    const QString secretKeyText = root.value("secretKey").toString();

    if ( ( version != 1 ) ||
         storedDeviceId.isEmpty() ||
         publicKeyText.isEmpty() ||
         secretKeyText.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("identity file is missing required fields: %1").arg(path);
        }
        return false;
    }

    QByteArray publicKey;
    QByteArray secretKey;
    if ( !CryptoEncoding::fromBase64Url(publicKeyText, &publicKey) ||
         !CryptoEncoding::fromBase64Url(secretKeyText, &secretKey) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "identity file contains invalid base64url key data: %1"
            ).arg(path);
        }
        return false;
    }

    const bool publicKeyLengthValid =
        publicKey.size() == DeviceAuthConstants::ed25519PublicKeyBytes;
    const bool secretKeyLengthValid =
        secretKey.size() == DeviceAuthConstants::ed25519SecretKeySeedBytes ||
        secretKey.size() == DeviceAuthConstants::ed25519SecretKeyLegacyBytes;
    if ( !publicKeyLengthValid ||
         !secretKeyLengthValid )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("identity key size is invalid: %1").arg(path);
        }
        return false;
    }

    const QString derivedDeviceId = deriveDeviceId(publicKey);
    if ( derivedDeviceId.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to derive device id from stored key");
        }
        return false;
    }

    identity->deviceId = derivedDeviceId;
    identity->publicKey = publicKey;
    identity->secretKey = secretKey;

    if ( storedDeviceId.compare(derivedDeviceId, Qt::CaseInsensitive) != 0 )
    {
        QString persistError;
        if ( !persist(*identity, &persistError) )
        {
            qWarning().noquote() << QStringLiteral(
                "failed to normalize identity file device id: %1"
            ).arg(persistError);
        }
    }

    return true;
}

bool DeviceIdentityStore::createAndPersist(DeviceIdentity *identity, QString *error) const
{
    QByteArray publicKey;
    QByteArray secretKeySeed;
    if ( !generateEd25519KeyPair(&publicKey, &secretKeySeed, error) )
    {
        return false;
    }

    identity->publicKey = publicKey;
    identity->secretKey = secretKeySeed;
    identity->deviceId = deriveDeviceId(publicKey);
    if ( identity->deviceId.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to derive device id from generated key");
        }
        return false;
    }

    return persist(*identity, error);
}

bool DeviceIdentityStore::persist(const DeviceIdentity &identity, QString *error) const
{
    const QString path = resolvedPath();
    const QFileInfo fileInfo(path);
    const QDir dir = fileInfo.dir();
    if ( !dir.exists() &&
         !dir.mkpath(".") )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to create identity directory: %1").arg(dir.path());
        }
        return false;
    }

    QJsonObject root;
    root.insert("version", 1);
    root.insert("deviceId", identity.deviceId);
    root.insert("publicKey", CryptoEncoding::toBase64Url(identity.publicKey));
    root.insert("secretKey", CryptoEncoding::toBase64Url(identity.secretKey));
    root.insert("createdAtMs", QDateTime::currentMSecsSinceEpoch());

    QSaveFile file(path);
    if ( !file.open(QIODevice::WriteOnly | QIODevice::Truncate) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to write identity file: %1").arg(path);
        }
        return false;
    }

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented) + '\n';
    if ( file.write(json) != json.size() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to persist identity file: %1").arg(path);
        }
        return false;
    }

    if ( !file.commit() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to commit identity file: %1").arg(path);
        }
        return false;
    }

    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString DeviceIdentityStore::resolvedPath() const
{
    if ( !identityPath_.isEmpty() )
    {
        return identityPath_;
    }
    return defaultIdentityPath();
}
