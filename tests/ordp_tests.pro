QT += core gui qml quick quickcontrols2 network testlib
CONFIG += c++17 testcase
TARGET = ordp_tests
TEMPLATE = app

INCLUDEPATH += ../src
include(../src/sources.pri)

SOURCES += ordp_tests.cpp
RESOURCES += ../src/resources.qrc
