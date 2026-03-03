PRO_PATH = $$PWD
TARGET   = JQOpenClawNode

TEMPLATE = app

QT += core gui qml quick network websockets widgets
CONFIG += console c++17
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../../modules

include( $$PWD/../../modules/capabilities.pri )
include( $$PWD/../../modules/openclawprotocol.pri )
include( $$PWD/../../modules/crypto.pri )
include( $$PWD/../../modules/jqcontrols.pri )

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
