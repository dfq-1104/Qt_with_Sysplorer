QT += core gui widgets

TEMPLATE = app
TARGET = sdk_probe
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= windows

SDK = "C:/Program Files/MWORKS/Sysplorer SDK 2026a(vs2017)"

INCLUDEPATH += $$SDK/interface $$SDK/include
DEPENDPATH  += $$SDK/interface

LIBS += -L$$SDK/bin/win_msvc2017x64/lib
LIBS += -lmw_class_manager -lmw_sim_inst -lmw_sim_plot -lmw_develop \
        -lmw_extension_system -lmw_model_var_tree -lmw_global -lmw_utility \
        -lmw_widgets -lmw_log

QMAKE_CXXFLAGS += /utf-8
DEFINES += UNICODE _UNICODE

SOURCES += main.cpp
QMAKE_CXXFLAGS += /wd4828

LIBS += -lmodelica_post_processor
QMAKE_CXXFLAGS_RELEASE = /Od /MD /Zi /RTC1
LIBS += -lmw_license_ui
