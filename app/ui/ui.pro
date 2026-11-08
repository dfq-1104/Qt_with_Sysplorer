QT += core gui widgets network
RESOURCES += models.qrc

TEMPLATE = app
TARGET = battery_console_ui
CONFIG += c++11
CONFIG -= app_bundle console
QMAKE_CXXFLAGS += /utf-8

INCLUDEPATH += src

SOURCES += src/main.cpp src/runtime_environment.cpp \
           src/main_window.cpp \
           src/battery_view.cpp \
           src/plot_widget.cpp \
           src/replay_data.cpp \
           src/replay_player.cpp \
           src/sysplorer_runner.cpp

HEADERS += src/main_window.h src/runtime_environment.h \
           src/battery_view.h \
           src/plot_widget.h \
           src/replay_data.h \
           src/replay_player.h \
           src/sysplorer_runner.h
INCLUDEPATH += ../../common
HEADERS += ../../common/remote_link.h
SOURCES += ../../common/remote_link.cpp
win32:LIBS += -lcrypt32

