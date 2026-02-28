HEADERS *= \
    $$PWD/crypto/cryptoencoding.h \
    $$PWD/crypto/deviceidentity/deviceidentity.h \
    $$PWD/crypto/secretbox/secretboxcrypto.h \
    $$PWD/crypto/signing/deviceauth.h

SOURCES *= \
    $$PWD/crypto/cryptoencoding.cpp \
    $$PWD/crypto/deviceidentity/deviceidentity.cpp \
    $$PWD/crypto/secretbox/secretboxcrypto.cpp \
    $$PWD/crypto/signing/deviceauth.cpp

include( $$PWD/openssl.pri )
