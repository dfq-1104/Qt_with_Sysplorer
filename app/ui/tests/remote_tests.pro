QT += core gui widgets network testlib
TEMPLATE = app
TARGET = remote_integration_test
CONFIG += console c++11 testcase
QMAKE_CXXFLAGS += /utf-8
INCLUDEPATH += ../src ../../../common
SOURCES += remote_integration_test.cpp ../src/main_window.cpp ../src/battery_view.cpp \
    ../src/plot_widget.cpp ../src/replay_data.cpp ../src/replay_player.cpp ../src/sysplorer_runner.cpp \
    ../../../common/remote_link.cpp ../../../common/remote_server.cpp
HEADERS += ../src/main_window.h ../src/battery_view.h ../src/plot_widget.h \
    ../src/replay_data.h ../src/replay_player.h ../src/sysplorer_runner.h \
    ../../../common/remote_link.h ../../../common/remote_server.h
win32:LIBS += -lcrypt32
HEADERS += ../../../tools/sysplorer_udp_plugin/task_status_compat.h
