// .h include
#include "crypto/signing/deviceauth.h"

// OpenSSL lib import
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>

// JQOpenClaw import
#include "crypto/cryptoencoding.h"

namespace {
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
}

bool DeviceAuth::initialize(QString *error)
{
    if ( OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr) != 1 )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("OpenSSL initialization failed: %1").arg(lastOpenSslError());
        }
        return false;
    }
    return true;
}

QString DeviceAuth::buildPayloadV3(const DeviceAuthPayloadInput &input)
{
    const QString scopes = input.scopes.join(',');
    const QString token = input.token;
    const QString platform = CryptoEncoding::normalizeMetadataForAuth(input.platform);
    const QString deviceFamily = CryptoEncoding::normalizeMetadataForAuth(input.deviceFamily);

    return QStringLiteral("v3|%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
        .arg(
            input.deviceId,
            input.clientId,
            input.clientMode,
            input.role,
            scopes,
            QString::number(input.signedAtMs),
            token,
            input.nonce,
            platform,
            deviceFamily
        );
}

bool DeviceAuth::normalizeSecretKeySeed(
    const QByteArray &secretKey,
    QByteArray *secretKeySeed,
    QString *error
)
{
    if ( secretKeySeed == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("secret key seed output pointer is null");
        }
        return false;
    }

    if ( secretKey.size() == DeviceAuthConstants::ed25519SecretKeySeedBytes )
    {
        *secretKeySeed = secretKey;
        return true;
    }

    if ( secretKey.size() == DeviceAuthConstants::ed25519SecretKeyLegacyBytes )
    {
        *secretKeySeed = secretKey.left(DeviceAuthConstants::ed25519SecretKeySeedBytes);
        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral(
            "invalid Ed25519 secret key size: %1 (expected 32 or 64)"
        ).arg(secretKey.size());
    }
    return false;
}

bool DeviceAuth::signDetached(
    const QByteArray &secretKey,
    const QByteArray &payload,
    QString *signatureBase64Url,
    QString *error
)
{
    if ( signatureBase64Url == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("signature output pointer is null");
        }
        return false;
    }

    QByteArray secretKeySeed;
    if ( !normalizeSecretKeySeed(secretKey, &secretKeySeed, error) )
    {
        return false;
    }

    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        nullptr,
        reinterpret_cast<const unsigned char *>(secretKeySeed.constData()),
        static_cast<size_t>(secretKeySeed.size())
    );
    if ( pkey == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to create Ed25519 private key: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    EVP_MD_CTX *mdCtx = EVP_MD_CTX_new();
    if ( mdCtx == nullptr )
    {
        EVP_PKEY_free(pkey);
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to create OpenSSL digest context");
        }
        return false;
    }

    const bool initOk = EVP_DigestSignInit(mdCtx, nullptr, nullptr, nullptr, pkey) == 1;
    if ( !initOk )
    {
        EVP_MD_CTX_free(mdCtx);
        EVP_PKEY_free(pkey);
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to initialize Ed25519 signer: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    size_t signatureLength = static_cast<size_t>(DeviceAuthConstants::ed25519SignatureBytes);
    QByteArray signature(static_cast<int>(signatureLength), Qt::Uninitialized);
    const bool signOk = EVP_DigestSign(
        mdCtx,
        reinterpret_cast<unsigned char *>(signature.data()),
        &signatureLength,
        reinterpret_cast<const unsigned char *>(payload.constData()),
        static_cast<size_t>(payload.size())
    ) == 1;

    EVP_MD_CTX_free(mdCtx);
    EVP_PKEY_free(pkey);

    if ( !signOk
        || signatureLength != static_cast<size_t>(DeviceAuthConstants::ed25519SignatureBytes) )
        {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to sign device auth payload: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    signature.resize(static_cast<int>(signatureLength));
    *signatureBase64Url = CryptoEncoding::toBase64Url(signature);
    return true;
}
