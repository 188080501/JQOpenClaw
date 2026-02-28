PRO_PATH = $$PWD
TARGET   = JQOpenClawNode

TEMPLATE = app

QT += core gui network websockets
CONFIG += console c++17
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../../modules

include( $$PWD/../../modules/capabilities.pri )
include( $$PWD/../../modules/openclawprotocol.pri )
include( $$PWD/../../modules/crypto.pri )

HEADERS *= \
    $$PWD/nodeapplication.h

SOURCES *= \
    $$PWD/main.cpp \
    $$PWD/nodeapplication.cpp
