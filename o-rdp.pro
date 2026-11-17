QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = o-rdp
TEMPLATE = app

include(src/sources.pri)

SOURCES += src/main.cpp
RESOURCES += src/resources.qrc
