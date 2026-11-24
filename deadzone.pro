QT += core gui qml quick quickcontrols2

CONFIG += c++20 release
TARGET = deadzone
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/curves.h \
    src/gamepads.h \
    src/lightbar.h \
    src/session.h \
    src/remapper.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/curves.cpp \
    src/gamepads.cpp \
    src/lightbar.cpp \
    src/session.cpp \
    src/remapper.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
