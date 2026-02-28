# OpenSSL integration notes (Windows / qmake):
# - Required: OPENSSL_ROOT points to local OpenSSL install directory.
#   Default fallback: C:/Develop/OpenSSL
# - Linkage (static/dynamic) is auto-detected from current Qt kit/build spec.
#   Optional manual override is still supported via OPENSSL_LINKAGE.
# - This project links libcrypto only (no libssl for now).
# - Supported header layouts:
#   1) <OPENSSL_ROOT>/include/openssl
#   2) <OPENSSL_ROOT>/include/<arch>/openssl   (WinUniversal installer)
# - Supported OpenSSL directory layouts:
#   1) Tiered layout:
#      <OPENSSL_ROOT>/<arch>/<Debug|Release>/<v142|v143>/<static|dynamic>/libcrypto*.lib
#   2) WinUniversal layout:
#      <OPENSSL_ROOT>/lib/VC/<arch>/<MD|MDd|MT|MTd>/libcrypto*.lib
# - For dynamic linkage, libcrypto DLL is copied to DESTDIR automatically when found.
# - Current verified package source:
#   slproweb WinUniversal OpenSSL v3.6.1 (2026-02-28)

OPENSSL_ROOT = $$clean_path($$(OPENSSL_ROOT))
isEmpty(OPENSSL_ROOT) {
    OPENSSL_ROOT = C:/Develop/OpenSSL
}

isEmpty(OPENSSL_ROOT) {
    error("OPENSSL_ROOT is not set. Example: set OPENSSL_ROOT=C:/Develop/OpenSSL")
}

OPENSSL_QMAKE_QMAKE = $$lower($$QMAKE_QMAKE)
OPENSSL_QMAKE_SPEC = $$lower($$QMAKE_SPEC)

OPENSSL_LINKAGE_OVERRIDE = $$lower($$OPENSSL_LINKAGE)
isEmpty(OPENSSL_LINKAGE_OVERRIDE) {
    OPENSSL_LINKAGE_OVERRIDE = $$lower($$(OPENSSL_LINKAGE))
}

# Auto detect linkage from kit/spec; allow explicit override if needed.
OPENSSL_LINKAGE = dynamic
contains(QT_CONFIG, static): OPENSSL_LINKAGE = static
else: contains(OPENSSL_QMAKE_QMAKE, static): OPENSSL_LINKAGE = static
else: contains(OPENSSL_QMAKE_SPEC, static): OPENSSL_LINKAGE = static

!isEmpty(OPENSSL_LINKAGE_OVERRIDE) {
    OPENSSL_LINKAGE = $$OPENSSL_LINKAGE_OVERRIDE
}

!equals(OPENSSL_LINKAGE, static):!equals(OPENSSL_LINKAGE, dynamic) {
    error("OPENSSL_LINKAGE must be static or dynamic")
}

contains(QMAKE_HOST.os, Windows) {

    OPENSSL_CONFIG_DIR = Release
    CONFIG(debug, debug|release) {
        OPENSSL_CONFIG_DIR = Debug
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

    OPENSSL_USE_QT_BUNDLED = false
    OPENSSL_QT_BUNDLED_ROOT =
    OPENSSL_QT_BUNDLED_DIR_CANDIDATES = $$files($$[QT_INSTALL_PREFIX]/openssl-*)
    for(bundleDir, OPENSSL_QT_BUNDLED_DIR_CANDIDATES) {
        exists($$bundleDir/include/openssl/crypto.h) {
            OPENSSL_QT_BUNDLED_ROOT = $$bundleDir
            break()
        }
    }

    OPENSSL_INCLUDE_DIR =
    contains(QT_CONFIG, static):!isEmpty(OPENSSL_QT_BUNDLED_ROOT) {
        # Static Qt may already inject libssl/libcrypto; reuse bundled headers and avoid duplicate lib link.
        OPENSSL_USE_QT_BUNDLED = true
        OPENSSL_INCLUDE_DIR = $$OPENSSL_QT_BUNDLED_ROOT/include
    } else {
        OPENSSL_INCLUDE_CANDIDATES = $$OPENSSL_ROOT/include/$$OPENSSL_ARCH $$OPENSSL_ROOT/include
        for(includeDir, OPENSSL_INCLUDE_CANDIDATES) {
            exists($$includeDir/openssl/crypto.h) {
                OPENSSL_INCLUDE_DIR = $$includeDir
                break()
            }
        }
    }
    isEmpty(OPENSSL_INCLUDE_DIR) {
        OPENSSL_INCLUDE_NOT_FOUND_ERROR = OpenSSL headers were not found. Expected: <OPENSSL_ROOT>/include/openssl or <OPENSSL_ROOT>/include/<arch>/openssl or <QT_INSTALL_PREFIX>/openssl-*/include/openssl
        error($$OPENSSL_INCLUDE_NOT_FOUND_ERROR)
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

    OPENSSL_ACTIVE_CFLAGS = $$QMAKE_CFLAGS
    CONFIG(debug, debug|release) {
        OPENSSL_ACTIVE_CFLAGS *= $$QMAKE_CFLAGS_DEBUG
    } else {
        OPENSSL_ACTIVE_CFLAGS *= $$QMAKE_CFLAGS_RELEASE
    }
    OPENSSL_ACTIVE_CFLAGS = $$lower($$OPENSSL_ACTIVE_CFLAGS)

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

    OPENSSL_LIB =
    OPENSSL_DIR =
    OPENSSL_LAYOUT =

    !equals(OPENSSL_USE_QT_BUNDLED, true) {
        OPENSSL_LIB_NAMES =
        equals(OPENSSL_LINKAGE, static) {
            OPENSSL_LIB_NAMES = libcrypto_static.lib libcrypto.lib
        } else {
            OPENSSL_LIB_NAMES = libcrypto.lib
        }

        for(toolset, OPENSSL_TOOLSET_CANDIDATES) {
            candidateDir = $$OPENSSL_ROOT/$$OPENSSL_ARCH/$$OPENSSL_CONFIG_DIR/$$toolset/$$OPENSSL_LINKAGE
            for(libName, OPENSSL_LIB_NAMES) {
                candidateLib = $$candidateDir/$$libName
                exists($$candidateLib) {
                    OPENSSL_DIR = $$candidateDir
                    OPENSSL_LIB = $$candidateLib
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
                        OPENSSL_DIR = $$candidateDir
                        OPENSSL_LIB = $$candidateLib
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
            OPENSSL_LIB_NOT_FOUND_ERROR = OpenSSL libcrypto library was not found. Supported layouts: <OPENSSL_ROOT>/<arch>/<Debug|Release>/<v142|v143>/<static|dynamic>/libcrypto*.lib or <OPENSSL_ROOT>/lib/VC/<arch>/<MD|MDd|MT|MTd>/libcrypto*.lib
            error($$OPENSSL_LIB_NOT_FOUND_ERROR)
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
                candidateDll = $$OPENSSL_DIR/$$dllName
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
                OPENSSL_DLL_NOT_FOUND_WARNING = OpenSSL dynamic dll was not found; runtime may fail without proper PATH
                warning($$OPENSSL_DLL_NOT_FOUND_WARNING)
            }
        }
    }

    equals(OPENSSL_USE_QT_BUNDLED, true) {
        message("OpenSSL: using Qt bundled OpenSSL from $$OPENSSL_QT_BUNDLED_ROOT (include=$$OPENSSL_INCLUDE_DIR, no extra lib link)")
    } else {
        message("OpenSSL: linkage=$$OPENSSL_LINKAGE, arch=$$OPENSSL_ARCH, runtime=$$OPENSSL_MSVC_RUNTIME, include=$$OPENSSL_INCLUDE_DIR, lib=$$OPENSSL_LIB")
    }
} else {
    error("This initial project currently supports Windows only.")
}
