#ifndef JQOPENCLAW_CRYPTO_SECRETBOX_SECRETBOXCRYPTO_H_
#define JQOPENCLAW_CRYPTO_SECRETBOX_SECRETBOXCRYPTO_H_

// Qt lib import
#include <QByteArray>
#include <QString>

class SecretBoxCrypto
{
public:
    static QByteArray generateKey();
    static bool encrypt(
        const QByteArray &key,
        const QByteArray &plainText,
        QByteArray *nonce,
        QByteArray *cipherText,
        QString *error
    );
    static bool decrypt(
        const QByteArray &key,
        const QByteArray &nonce,
        const QByteArray &cipherText,
        QByteArray *plainText,
        QString *error
    );
};

#endif // JQOPENCLAW_CRYPTO_SECRETBOX_SECRETBOXCRYPTO_H_
