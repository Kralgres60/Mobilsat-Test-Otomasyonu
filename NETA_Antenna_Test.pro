# ------------------------------------------------------------------------------
# NETA DRX9000 Çoklu İstasyonlu Anten Test Otomasyonu
# Tek İkili Proje (All-in-One Binary: Host & Client Modları Destekli)
# ------------------------------------------------------------------------------

QT += core gui widgets network serialport printsupport sql gui-private

CONFIG += c++17

win32-g++ {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
}

TARGET = NETA_Antenna_Test
VERSION = 1.0.0
TEMPLATE = app

INCLUDEPATH += . src src/core src/network src/database src/hardware src/ui src/ui/common src/ui/client src/ui/host

RESOURCES += resources.qrc

win32 {
    RC_ICONS = resources/images/neta_app.ico

    # MySQL ve QMYSQL eklentisinin cikti dizinine otomatik kopyalanmasi
    QMAKE_POST_LINK += $(COPY_DIR) $$shell_path($$PWD/tools/sqldrivers) $$shell_path($$OUT_PWD/debug/sqldrivers) $$escape_expand(\n\t)
    QMAKE_POST_LINK += $(COPY) $$shell_path($$PWD/tools/sqldrivers/libmysql.dll) $$shell_path($$OUT_PWD/debug/)
}

# Kaynak Dosyalar (Sources)
SOURCES += \
    src/main.cpp \
    src/core/app_config.cpp \
    src/core/app_logger.cpp \
    src/core/lock_arbiter.cpp \
    src/core/test_state_machine.cpp \
    src/core/dependency_installer.cpp \
    src/core/xlsxutil.cpp \
    src/database/db_manager.cpp \
    src/hardware/protocolhandler.cpp \
    src/hardware/serial_worker.cpp \
    src/hardware/drx_web_client.cpp \
    src/network/udp_beacon.cpp \
    src/network/tcp_server.cpp \
    src/network/tcp_client.cpp \
    src/ui/common/style_helper.cpp \
    src/ui/common/mode_select_dialog.cpp \
    src/ui/common/login_dialog.cpp \
    src/ui/common/lobby_window.cpp \
    src/ui/common/about_dialog.cpp \
    src/ui/common/profile_manager_widget.cpp \
    src/ui/common/dependency_install_dialog.cpp \
    src/ui/common/qcustomplot.cpp \
    src/ui/client/channel_card_widget.cpp \
    src/ui/client/client_window.cpp \
    src/ui/host/golden_reference_widget.cpp \
    src/ui/host/db_viewer_widget.cpp \
    src/ui/host/host_window.cpp \
    src/ui/debug/debug_runner_window.cpp \
    src/ui/debug/db_debug_widget.cpp

# Başlık Dosyaları (Headers)
HEADERS += \
    src/core/build_config.h \
    src/core/appinfo.h \
    src/core/app_config.h \
    src/core/app_logger.h \
    src/core/simulator_launcher.h \
    src/core/data_models.h \
    src/core/lock_arbiter.h \
    src/core/test_state_machine.h \
    src/core/dependency_installer.h \
    src/core/xlsxutil.h \
    src/database/db_manager.h \
    src/hardware/protocol_commands.h \
    src/hardware/protocolhandler.h \
    src/hardware/serial_worker.h \
    src/hardware/drx_web_client.h \
    src/network/network_messages.h \
    src/network/udp_beacon.h \
    src/network/tcp_server.h \
    src/network/tcp_client.h \
    src/ui/common/style_helper.h \
    src/ui/common/mode_select_dialog.h \
    src/ui/common/login_dialog.h \
    src/ui/common/lobby_window.h \
    src/ui/common/about_dialog.h \
    src/ui/common/profile_manager_widget.h \
    src/ui/common/dependency_install_dialog.h \
    src/ui/common/qcustomplot.h \
    src/ui/client/channel_card_widget.h \
    src/ui/client/client_window.h \
    src/ui/host/golden_reference_widget.h \
    src/ui/host/db_viewer_widget.h \
    src/ui/host/host_window.h \
    src/ui/debug/debug_runner_window.h \
    src/ui/debug/db_debug_widget.h
