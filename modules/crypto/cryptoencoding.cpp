// .h include
#include "crypto/cryptoencoding.h"

namespace {
QString toLowerAscii(const QString &value)
{
    QString out = value;
    for ( int index = 0; index < out.size(); ++index )
    {
        const QChar ch = out.at(index);
        if ( ch >= QLatin1Char('A')
            && ch <= QLatin1Char('Z') )
            {
            out[index] = QChar(ch.unicode() + 32);
        }
    }
    return out;
}

bool isBase64UrlChar(const QChar ch)
{
    return (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
           || (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
           || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
           || ch == QLatin1Char('-')
           || ch == QLatin1Char('_')
           || ch == QLatin1Char('=');
}

bool hasOnlyBase64UrlChars(const QString &value)
{
    for ( int i = 0; i < value.size(); ++i )
    {
        if ( !isBase64UrlChar(value.at(i)) )
        {
            return false;
        }
    }
    return true;
}
}

QString CryptoEncoding::toBase64Url(const QByteArray &data)
{
    QByteArray encoded = data.toBase64();
    encoded.replace('+', '-');
    encoded.replace('/', '_');
    while ( encoded.endsWith('=') )
    {
        encoded.chop(1);
    }
    return QString::fromLatin1(encoded);
}

bool CryptoEncoding::fromBase64Url(const QString &text, QByteArray *data)
{
    if ( data == nullptr )
    {
        return false;
    }

    const QString trimmed = text.trimmed();
    if ( !hasOnlyBase64UrlChars(trimmed) )
    {
        return false;
    }

    const int firstPaddingIndex = trimmed.indexOf('=');
    if ( firstPaddingIndex >= 0 )
    {
        if ( firstPaddingIndex == 0 )
        {
            return false;
        }
        for ( int i = firstPaddingIndex; i < trimmed.size(); ++i )
        {
            if ( trimmed.at(i) != QLatin1Char('=') )
            {
                return false;
            }
        }
        if ( trimmed.size() - firstPaddingIndex > 2 )
        {
            return false;
        }
    }

    QByteArray normalized = trimmed.toLatin1();
    normalized.replace('-', '+');
    normalized.replace('_', '/');

    const int remainder = normalized.size() % 4;
    if ( remainder != 0 )
    {
        normalized.append(QByteArray(4 - remainder, '='));
    }

    const QByteArray decoded = QByteArray::fromBase64(normalized, QByteArray::Base64Encoding);
    if ( decoded.isEmpty()
        && !normalized.isEmpty() )
        {
        return false;
    }

    QString canonicalInput = trimmed;
    while ( canonicalInput.endsWith(QLatin1Char('=')) )
    {
        canonicalInput.chop(1);
    }
    if ( toBase64Url(decoded) != canonicalInput )
    {
        return false;
    }

    *data = decoded;
    return true;
}

QString CryptoEncoding::normalizeMetadataForAuth(const QString &value)
{
    const QString trimmed = value.trimmed();
    if ( trimmed.isEmpty() )
    {
        return QString();
    }
    return toLowerAscii(trimmed);
}

QString CryptoEncoding::normalizeFingerprint(const QString &value)
{
    QString fingerprint = toLowerAscii(value.trimmed());
    fingerprint.remove(':');
    fingerprint.remove('-');
    fingerprint.remove(' ');
    return fingerprint;
}
