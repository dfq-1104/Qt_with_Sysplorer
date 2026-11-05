QT += core gui widgets network
RESOURCES += models.qrc

TEMPLATE = app
TARGET = battery_console_ui
CONFIG += c++11
CONFIG -= app_bundle console
QMAKE_CXXFLAGS += /utf-8

INCLUDEPATH += src

SOURCES += src/main.cpp \
           src/main_window.cpp \
           src/battery_view.cpp \
           src/plot_widget.cpp \
           src/replay_data.cpp \
           src/replay_player.cpp \
           src/sysplorer_runner.cpp

HEADERS += src/main_window.h \
           src/battery_view.h \
           src/plot_widget.h \
           src/replay_data.h \
           src/replay_player.h \
           src/sysplorer_runner.h

