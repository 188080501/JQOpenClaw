#ifndef JQOPENCLAW_CRYPTO_CRYPTOENCODING_H_
#define JQOPENCLAW_CRYPTO_CRYPTOENCODING_H_

// Qt lib import
#include <QByteArray>
#include <QString>

namespace CryptoEncoding
{
QString toBase64Url(const QByteArray &data);
bool fromBase64Url(const QString &text, QByteArray *data);
QString normalizeMetadataForAuth(const QString &value);
QString normalizeFingerprint(const QString &value);
}

#endif // JQOPENCLAW_CRYPTO_CRYPTOENCODING_H_
