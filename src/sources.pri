# Every source the app and the tests both compile. main.cpp stays out.
HEADERS += \
    $$PWD/backend.h \
    $$PWD/cli.h \
    $$PWD/hosts.h \
    $$PWD/keyring.h \
    $$PWD/scanner.h \
    $$PWD/theme.h \
    $$PWD/translator.h

SOURCES += \
    $$PWD/backend.cpp \
    $$PWD/cli.cpp \
    $$PWD/hosts.cpp \
    $$PWD/keyring.cpp \
    $$PWD/scanner.cpp \
    $$PWD/theme.cpp \
    $$PWD/translator.cpp
