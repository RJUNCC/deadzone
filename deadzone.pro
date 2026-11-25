QT += core gui qml quick quickcontrols2 network

CONFIG += c++20 release qtquickcompiler
# Link-time optimisation and -O2 keep the remapper path short.
unix: QMAKE_CXXFLAGS_RELEASE += -O2
CONFIG += ltcg
TARGET = deadzone
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/curves.h \
    src/gamepads.h \
    src/inputcodes.h \
    src/lightbar.h \
    src/session.h \
    src/history.h \
    src/overlay.h \
    src/clipboard.h \
    src/remapper.h \
    src/theme.h \
    src/translator.h \
    src/windowlock.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/curves.cpp \
    src/lightbar.cpp \
    src/session.cpp \
    src/history.cpp \
    src/overlay.cpp \
    src/remapper.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/windowlock.cpp

RESOURCES += src/resources.qrc

unix {
    SOURCES += src/backend_linux.cpp src/gamepads.cpp src/remapper_linux.cpp
}

# Windows reads controllers through SDL 3 and hands games a virtual Xbox pad
# through ViGEmBus. SDL3_DIR is an unpacked SDL3-devel-*-VC release and
# VIGEM_DIR a checkout of nefarius/ViGEmClient (bin/windows-deps fetches both).
win32 {
    isEmpty(SDL3_DIR): SDL3_DIR = $$PWD/windeps/SDL3
    isEmpty(VIGEM_DIR): VIGEM_DIR = $$PWD/windeps/ViGEmClient
    SOURCES += src/backend_win.cpp src/gamepads_win.cpp src/remapper_win.cpp \
               $$VIGEM_DIR/src/ViGEmClient.cpp
    INCLUDEPATH += $$SDL3_DIR/include $$VIGEM_DIR/include $$VIGEM_DIR/src
    LIBS += -L$$SDL3_DIR/lib/x64 -lSDL3 -lsetupapi -luser32 -ladvapi32 -lshell32 -lwinmm
    DEFINES += NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES
    QMAKE_CXXFLAGS += /utf-8
    RC_ICONS = pkgbuild/deadzone.ico
    QMAKE_TARGET_DESCRIPTION = Deadzone
    VERSION = 0.11.0
}
