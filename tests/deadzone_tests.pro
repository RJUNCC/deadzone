QT += core gui quick quickcontrols2 testlib
CONFIG += c++20 testcase
TARGET = deadzone_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/curves.h ../src/gamepads.h ../src/lightbar.h ../src/session.h ../src/remapper.h ../src/theme.h ../src/translator.h
SOURCES += deadzone_tests.cpp ../src/backend.cpp ../src/curves.cpp ../src/gamepads.cpp ../src/lightbar.cpp ../src/session.cpp ../src/remapper.cpp \
           ../src/theme.cpp ../src/translator.cpp
