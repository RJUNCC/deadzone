QT += core gui qml quick quickcontrols2 network

CONFIG += c++20 release qtquickcompiler
# Link-time optimisation and -O2 keep the remapper path short.
QMAKE_CXXFLAGS_RELEASE += -O2
CONFIG += ltcg
TARGET = deadzone
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/curves.h \
    src/gamepads.h \
    src/lightbar.h \
    src/session.h \
    src/history.h \
    src/overlay.h \
    src/clipboard.h \
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
    src/history.cpp \
    src/overlay.cpp \
    src/remapper.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
