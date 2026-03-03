PRO_PATH = $$PWD
TARGET   = JQOpenClawNode

TEMPLATE = app

QT += core gui qml quick quickcontrols2 network websockets widgets
CONFIG += c++17
CONFIG -= app_bundle

VERSION  = 1.0.0
DEFINES *= JQOPENCLAWNODE_VERSION='\\"$$VERSION\\"'

QMAKE_TARGET_COMPANY     = "JQOpenClaw"
QMAKE_TARGET_DESCRIPTION = $$TARGET
QMAKE_TARGET_COPYRIGHT   = "Copyright (c) 2026 Jason and others"

include( $$PWD/../../modules/capabilities.pri )
include( $$PWD/../../modules/openclawprotocol.pri )
include( $$PWD/../../modules/crypto.pri )
include( $$PWD/../../modules/jqcontrols.pri )

INCLUDEPATH *= \
    $$PWD/../../modules

HEADERS *= \
    $$PWD/cpp/nodeapplication.h \
    $$PWD/cpp/nodeapplication.inc

SOURCES *= \
    $$PWD/cpp/main.cpp \
    $$PWD/cpp/nodeapplication.cpp

RESOURCES *= \
    $$PWD/qml/qml.qrc \
    $$PWD/../../icon/icon.qrc

QML_IMPORT_PATH *= \
    $$PWD/qml

win32 {
    RC_ICONS = $$PWD/../../icon/icon.ico
}
