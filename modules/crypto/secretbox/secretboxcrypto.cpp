// .h include
#include "crypto/secretbox/secretboxcrypto.h"

// OpenSSL lib import
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace
{
constexpr int secretBoxKeyBytes = 32;
constexpr int secretBoxNonceBytes = 12;
constexpr int secretBoxTagBytes = 16;

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

QByteArray SecretBoxCrypto::generateKey()
{
    QByteArray key(secretBoxKeyBytes, Qt::Uninitialized);
    if ( RAND_bytes(reinterpret_cast<unsigned char *>(key.data()), key.size()) != 1 )
    {
        return QByteArray();
    }
    return key;
}

bool SecretBoxCrypto::encrypt(
    const QByteArray &key,
    const QByteArray &plainText,
    QByteArray *nonce,
    QByteArray *cipherText,
    QString *error
)
{
    if ( nonce == nullptr
        || cipherText == nullptr )
        {
        if ( error != nullptr )
        {
            *error = QStringLiteral("nonce or cipher output pointer is null");
        }
        return false;
    }

    if ( key.size() != secretBoxKeyBytes )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid secretbox key size");
        }
        return false;
    }

    nonce->resize(secretBoxNonceBytes);
    if ( RAND_bytes(reinterpret_cast<unsigned char *>(nonce->data()), nonce->size()) != 1 )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to generate nonce: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if ( ctx == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to create cipher context");
        }
        return false;
    }

    const bool initOk =
        EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, secretBoxNonceBytes, nullptr) == 1
        && EVP_EncryptInit_ex(
               ctx,
               nullptr,
               nullptr,
               reinterpret_cast<const unsigned char *>(key.constData()),
               reinterpret_cast<const unsigned char *>(nonce->constData())
           ) == 1;
    if ( !initOk )
    {
        EVP_CIPHER_CTX_free(ctx);
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to initialize secretbox encryptor: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    QByteArray encrypted(plainText.size(), Qt::Uninitialized);
    int encryptedLength = 0;
    const bool updateOk = EVP_EncryptUpdate(
        ctx,
        reinterpret_cast<unsigned char *>(encrypted.data()),
        &encryptedLength,
        reinterpret_cast<const unsigned char *>(plainText.constData()),
        plainText.size()
    );
    if ( !updateOk )
    {
        EVP_CIPHER_CTX_free(ctx);
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "secretbox encryption failed: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    int finalLength = 0;
    const bool finalOk = EVP_EncryptFinal_ex(
        ctx,
        reinterpret_cast<unsigned char *>(encrypted.data()) + encryptedLength,
        &finalLength
    ) == 1;
    if ( !finalOk )
    {
        EVP_CIPHER_CTX_free(ctx);
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "secretbox encryption finalize failed: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    encrypted.resize(encryptedLength + finalLength);

    QByteArray tag(secretBoxTagBytes, Qt::Uninitialized);
    const bool tagOk = EVP_CIPHER_CTX_ctrl(
        ctx,
        EVP_CTRL_AEAD_GET_TAG,
        secretBoxTagBytes,
        tag.data()
    ) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if ( !tagOk )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("secretbox tag extraction failed: %1").arg(lastOpenSslError());
        }
        return false;
    }

    *cipherText = encrypted + tag;
    return true;
}

bool SecretBoxCrypto::decrypt(
    const QByteArray &key,
    const QByteArray &nonce,
    const QByteArray &cipherText,
    QByteArray *plainText,
    QString *error
)
{
    if ( plainText == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("plain output pointer is null");
        }
        return false;
    }

    if ( key.size() != secretBoxKeyBytes )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid secretbox key size");
        }
        return false;
    }

    if ( nonce.size() != secretBoxNonceBytes )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid secretbox nonce size");
        }
        return false;
    }

    if ( cipherText.size() < secretBoxTagBytes )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("cipher text is too short");
        }
        return false;
    }

    const int encryptedLength = cipherText.size() - secretBoxTagBytes;
    const QByteArray encrypted = cipherText.left(encryptedLength);
    const QByteArray tag = cipherText.right(secretBoxTagBytes);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if ( ctx == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to create cipher context");
        }
        return false;
    }

    const bool initOk =
        EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, secretBoxNonceBytes, nullptr) == 1
        && EVP_DecryptInit_ex(
               ctx,
               nullptr,
               nullptr,
               reinterpret_cast<const unsigned char *>(key.constData()),
               reinterpret_cast<const unsigned char *>(nonce.constData())
           ) == 1;
    if ( !initOk )
    {
        EVP_CIPHER_CTX_free(ctx);
        if ( error != nullptr )
        {
            *error = QStringLiteral(
                "failed to initialize secretbox decryptor: %1"
            ).arg(lastOpenSslError());
        }
        return false;
    }

    plainText->resize(encryptedLength);
    int plainTextLength = 0;
    const bool updateOk = EVP_DecryptUpdate(
        ctx,
        reinterpret_cast<unsigned char *>(plainText->data()),
        &plainTextLength,
        reinterpret_cast<const unsigned char *>(encrypted.constData()),
        encrypted.size()
    );
    if ( !updateOk )
    {
        EVP_CIPHER_CTX_free(ctx);
        plainText->clear();
        if ( error != nullptr )
        {
            *error = QStringLiteral("secretbox decryption failed: %1").arg(lastOpenSslError());
        }
        return false;
    }

    const bool setTagOk = EVP_CIPHER_CTX_ctrl(
        ctx,
        EVP_CTRL_AEAD_SET_TAG,
        secretBoxTagBytes,
        const_cast<unsigned char *>(
            reinterpret_cast<const unsigned char *>(tag.constData())
        )
    ) == 1;
    if ( !setTagOk )
    {
        EVP_CIPHER_CTX_free(ctx);
        plainText->clear();
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to set secretbox auth tag: %1").arg(lastOpenSslError());
        }
        return false;
    }

    int finalLength = 0;
    const bool finalOk = EVP_DecryptFinal_ex(
        ctx,
        reinterpret_cast<unsigned char *>(plainText->data()) + plainTextLength,
        &finalLength
    ) == 1;
    EVP_CIPHER_CTX_free(ctx);

    if ( !finalOk )
    {
        plainText->clear();
        if ( error != nullptr )
        {
            *error = QStringLiteral("secretbox decryption failed");
        }
        return false;
    }

    plainText->resize(plainTextLength + finalLength);
    return true;
}
