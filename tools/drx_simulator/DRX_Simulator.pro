# ------------------------------------------------------------------------------
# NETA DRX9000 Sanal Cihaz (Web CGI) Simülatörü
# Bağımsız Qt C++ Test Aracı (Python/ek kurulum gerektirmez)
# ------------------------------------------------------------------------------

QT += core gui widgets network

CONFIG += c++17

win32-g++ {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
}

TARGET = NETA_DRX_Simulator
TEMPLATE = app

SOURCES += \
    main.cpp \
    drx_sim_server.cpp \
    sim_curve_model.cpp \
    sim_curve_widget.cpp \
    simulator_window.cpp

HEADERS += \
    drx_sim_server.h \
    sim_curve_model.h \
    sim_curve_widget.h \
    simulator_window.h
