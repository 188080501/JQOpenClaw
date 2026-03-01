# OpenSSL integration (Windows / qmake)
# 详细说明见：docs/OpenSSL依赖.md

OPENSSL_ROOT = $$clean_path($$(OPENSSL_ROOT))
isEmpty(OPENSSL_ROOT) {
    OPENSSL_ROOT = C:/Develop/OpenSSL
}

isEmpty(OPENSSL_ROOT) {
    error("OPENSSL_ROOT is not set. Example: set OPENSSL_ROOT=C:/Develop/OpenSSL")
}

OPENSSL_LINKAGE = $$lower($$OPENSSL_LINKAGE)
isEmpty(OPENSSL_LINKAGE) {
    OPENSSL_LINKAGE = $$lower($$(OPENSSL_LINKAGE))
}
isEmpty(OPENSSL_LINKAGE) {
    OPENSSL_LINKAGE = dynamic
    contains(QT_CONFIG, static): OPENSSL_LINKAGE = static
}

!equals(OPENSSL_LINKAGE, static):!equals(OPENSSL_LINKAGE, dynamic) {
    error("OPENSSL_LINKAGE must be static or dynamic")
}

!contains(QMAKE_HOST.os, Windows) {
    error("This project currently supports Windows OpenSSL integration only.")
}

OPENSSL_ARCH_RAW = $$lower($$QT_ARCH)
isEmpty(OPENSSL_ARCH_RAW) {
    OPENSSL_ARCH_RAW = $$lower($$QMAKE_TARGET.arch)
}
isEmpty(OPENSSL_ARCH_RAW) {
    OPENSSL_ARCH_RAW = $$lower($$QMAKE_HOST.arch)
}

OPENSSL_ARCH = x64
contains(OPENSSL_ARCH_RAW, "x86_64") | contains(OPENSSL_ARCH_RAW, "amd64") {
    OPENSSL_ARCH = x64
} else: contains(OPENSSL_ARCH_RAW, "i386") | contains(OPENSSL_ARCH_RAW, "i686") | contains(OPENSSL_ARCH_RAW, "x86") {
    OPENSSL_ARCH = x86
} else: contains(OPENSSL_ARCH_RAW, "arm64") | contains(OPENSSL_ARCH_RAW, "aarch64") {
    OPENSSL_ARCH = arm64
}

OPENSSL_CONFIG_DIR = Release
CONFIG(debug, debug|release) {
    OPENSSL_CONFIG_DIR = Debug
}

OPENSSL_INCLUDE_DIR =
OPENSSL_INCLUDE_CANDIDATES = $$OPENSSL_ROOT/include/$$OPENSSL_ARCH $$OPENSSL_ROOT/include
for(includeDir, OPENSSL_INCLUDE_CANDIDATES) {
    exists($$includeDir/openssl/crypto.h) {
        OPENSSL_INCLUDE_DIR = $$includeDir
        break()
    }
}

isEmpty(OPENSSL_INCLUDE_DIR) {
    error("OpenSSL headers not found. Expected in <OPENSSL_ROOT>/include[/<arch>]/openssl.")
}

INCLUDEPATH += $$OPENSSL_INCLUDE_DIR

OPENSSL_MSVC_TOOLSET =
win32-msvc {
    greaterThan(QMAKE_MSC_VER, 1929) {
        OPENSSL_MSVC_TOOLSET = v143
    } else: greaterThan(QMAKE_MSC_VER, 1919) {
        OPENSSL_MSVC_TOOLSET = v142
    }
}

OPENSSL_TOOLSET_CANDIDATES =
equals(OPENSSL_MSVC_TOOLSET, v143) {
    OPENSSL_TOOLSET_CANDIDATES = v143 v142
} else: equals(OPENSSL_MSVC_TOOLSET, v142) {
    OPENSSL_TOOLSET_CANDIDATES = v142 v143
} else {
    OPENSSL_TOOLSET_CANDIDATES = v143 v142
}

OPENSSL_MSVC_RUNTIME = MD
CONFIG(debug, debug|release) {
    OPENSSL_MSVC_RUNTIME = MDd
}

OPENSSL_ACTIVE_CFLAGS = $$lower($$QMAKE_CFLAGS)
CONFIG(debug, debug|release) {
    OPENSSL_ACTIVE_CFLAGS *= $$QMAKE_CFLAGS_DEBUG
} else {
    OPENSSL_ACTIVE_CFLAGS *= $$QMAKE_CFLAGS_RELEASE
}

contains(OPENSSL_ACTIVE_CFLAGS, /mtd) {
    OPENSSL_MSVC_RUNTIME = MTd
} else: contains(OPENSSL_ACTIVE_CFLAGS, /mt) {
    OPENSSL_MSVC_RUNTIME = MT
} else: contains(OPENSSL_ACTIVE_CFLAGS, /mdd) {
    OPENSSL_MSVC_RUNTIME = MDd
} else: contains(OPENSSL_ACTIVE_CFLAGS, /md) {
    OPENSSL_MSVC_RUNTIME = MD
}

OPENSSL_MSVC_RUNTIME_CANDIDATES =
equals(OPENSSL_MSVC_RUNTIME, MTd) {
    OPENSSL_MSVC_RUNTIME_CANDIDATES = MTd MT MDd MD
} else: equals(OPENSSL_MSVC_RUNTIME, MT) {
    OPENSSL_MSVC_RUNTIME_CANDIDATES = MT MTd MD MDd
} else: equals(OPENSSL_MSVC_RUNTIME, MDd) {
    OPENSSL_MSVC_RUNTIME_CANDIDATES = MDd MD MTd MT
} else {
    OPENSSL_MSVC_RUNTIME_CANDIDATES = MD MDd MT MTd
}

OPENSSL_LIB_NAMES = libcrypto.lib
equals(OPENSSL_LINKAGE, static) {
    OPENSSL_LIB_NAMES = libcrypto_static.lib libcrypto.lib
}

OPENSSL_LIB =
OPENSSL_LIB_DIR =
OPENSSL_LAYOUT =

for(toolset, OPENSSL_TOOLSET_CANDIDATES) {
    candidateDir = $$OPENSSL_ROOT/$$OPENSSL_ARCH/$$OPENSSL_CONFIG_DIR/$$toolset/$$OPENSSL_LINKAGE
    for(libName, OPENSSL_LIB_NAMES) {
        candidateLib = $$candidateDir/$$libName
        exists($$candidateLib) {
            OPENSSL_LIB = $$candidateLib
            OPENSSL_LIB_DIR = $$candidateDir
            OPENSSL_LAYOUT = tiered
            break()
        }
    }
    !isEmpty(OPENSSL_LIB) {
        break()
    }
}

isEmpty(OPENSSL_LIB) {
    for(runtimeTag, OPENSSL_MSVC_RUNTIME_CANDIDATES) {
        candidateDir = $$OPENSSL_ROOT/lib/VC/$$OPENSSL_ARCH/$$runtimeTag
        for(libName, OPENSSL_LIB_NAMES) {
            candidateLib = $$candidateDir/$$libName
            exists($$candidateLib) {
                OPENSSL_LIB = $$candidateLib
                OPENSSL_LIB_DIR = $$candidateDir
                OPENSSL_LAYOUT = winuniversal
                break()
            }
        }
        !isEmpty(OPENSSL_LIB) {
            break()
        }
    }
}

isEmpty(OPENSSL_LIB) {
    error("OpenSSL libcrypto not found. Supported layouts are documented in docs/OpenSSL依赖.md.")
}

equals(OPENSSL_LINKAGE, static) {
    DEFINES += OPENSSL_USE_STATIC_LIBS
    LIBS += -lWs2_32 -lCrypt32 -lAdvapi32 -lUser32 -lBcrypt
}

LIBS += $$quote($$OPENSSL_LIB)

equals(OPENSSL_LINKAGE, dynamic) {
    OPENSSL_DLL =
    OPENSSL_DLL_CANDIDATES = libcrypto-3-x64.dll libcrypto-3-arm64.dll libcrypto-3.dll libcrypto.dll

    for(dllName, OPENSSL_DLL_CANDIDATES) {
        candidateDll = $$OPENSSL_LIB_DIR/$$dllName
        exists($$candidateDll) {
            OPENSSL_DLL = $$candidateDll
            break()
        }
    }

    isEmpty(OPENSSL_DLL) {
        for(dllName, OPENSSL_DLL_CANDIDATES) {
            candidateDll = $$OPENSSL_ROOT/bin/$$OPENSSL_ARCH/$$dllName
            exists($$candidateDll) {
                OPENSSL_DLL = $$candidateDll
                break()
            }
        }
    }

    isEmpty(OPENSSL_DLL) {
        for(dllName, OPENSSL_DLL_CANDIDATES) {
            candidateDll = $$OPENSSL_ROOT/bin/$$dllName
            exists($$candidateDll) {
                OPENSSL_DLL = $$candidateDll
                break()
            }
        }
    }

    !isEmpty(OPENSSL_DLL) {
        OPENSSL_DLL_SRC = $$replace($$OPENSSL_DLL, /, \\)
        OPENSSL_DLL_DST = $$replace($$DESTDIR/$$basename($$OPENSSL_DLL), /, \\)
        QMAKE_POST_LINK += copy /Y \"$$OPENSSL_DLL_SRC\" \"$$OPENSSL_DLL_DST\"
        QMAKE_POST_LINK += $$escape_expand(\\n\\t)
    } else {
        warning("OpenSSL dynamic DLL was not found; runtime may fail without PATH.")
    }
}

message("OpenSSL: linkage=$$OPENSSL_LINKAGE, arch=$$OPENSSL_ARCH, runtime=$$OPENSSL_MSVC_RUNTIME, include=$$OPENSSL_INCLUDE_DIR, lib=$$OPENSSL_LIB, layout=$$OPENSSL_LAYOUT")
