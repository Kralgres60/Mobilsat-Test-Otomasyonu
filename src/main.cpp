/**
 * @file main.cpp
 * @brief Application entry point, CLI argument parsing, operating role dispatch, and supervisor startup.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QCommandLineParser>
#include <QDebug>
#include <QTextCodec>
#include "core/appinfo.h"
#include "core/app_config.h"
#include "core/app_logger.h"
#include "core/build_config.h"
#include "core/dependency_installer.h"
#include "database/db_manager.h"
#include "ui/common/style_helper.h"
#include "ui/common/login_dialog.h"
#include "ui/common/lobby_window.h"
#include "ui/client/client_window.h"
#include "ui/host/host_window.h"
#if defined(NETA_DEBUG_MODE)
#include "ui/debug/debug_runner_window.h"
#endif
#if defined(NETA_DEBUG_DB_MODE)
#include "ui/debug/debug_runner_window.h"
#include <QMainWindow>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // Disable Qt5 bearer network adapter polling that stalls Windows startup (set to 24h to avoid negative interval warning)
    qputenv("QT_BEARER_POLL_TIMEOUT", QByteArray::number(86400000));

    QApplication app(argc, argv);
    app.setFont(QFont("Segoe UI", 9));
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    AppLogger::instance().init();
    AppLogger::instance().logEvent("APP", QString("NETA DRX9000 v%1 started.").arg(NETA_APP_VERSION));

#if defined(NETA_DEBUG_MODE)
    qInfo() << "==========================================================";
    qInfo() << " NETA DRX9000 ENGINE: [DEVELOPER & DEBUG TEST MODE ACTIVE]";
    qInfo() << "==========================================================";
    app.setApplicationName(NETA_APP_NAME);
    app.setApplicationVersion(NETA_APP_VERSION);
    app.setOrganizationName(NETA_APP_ORGANIZATION);
    app.setWindowIcon(QIcon(":/resources/images/neta_app.ico"));
    StyleHelper::applyAppPalette(&app);
    app.setStyleSheet(StyleHelper::getAppStyle());

    DebugRunnerWindow *debugWin = new DebugRunnerWindow();
    debugWin->show();
    return app.exec();
#endif

#if defined(NETA_DEBUG_DB_MODE)
    qInfo() << "==========================================================";
    qInfo() << " NETA DRX9000 ENGINE: [MARIADB & DISTRIBUTED LOCK DEBUG ACTIVE]";
    qInfo() << "==========================================================";
    app.setApplicationName(NETA_APP_NAME);
    app.setApplicationVersion(NETA_APP_VERSION);
    app.setOrganizationName(NETA_APP_ORGANIZATION);
    app.setWindowIcon(QIcon(":/resources/images/neta_app.ico"));
    StyleHelper::applyAppPalette(&app);
    app.setStyleSheet(StyleHelper::getAppStyle());

    QMainWindow *dbWin = new QMainWindow();
    dbWin->setWindowTitle("NETA DRX9000 - MariaDB & Dağıtık Kilit (Algoritma 2) Test Konsolu");
    dbWin->resize(1100, 720);
    dbWin->setCentralWidget(new DbDebugWidget(dbWin));
    dbWin->show();
    return app.exec();
#endif

#if defined(NETA_ALGORITMA_1_FOUR_PC)
    qInfo() << "==========================================================";
    qInfo() << " NETA DRX9000 ENGINE: [ALGORITHM 1: 4-PC CENTRAL MUTEX QUEUE]";
    qInfo() << "==========================================================";
#elif defined(NETA_ALGORITMA_2_THREE_PC)
    qInfo() << "==========================================================";
    qInfo() << " NETA DRX9000 ENGINE: [ALGORITHM 2: 3-PC DISTRIBUTED DB QUEUE]";
    qInfo() << "==========================================================";
#endif

    app.setApplicationName(NETA_APP_NAME);
    app.setApplicationVersion(NETA_APP_VERSION);
    app.setOrganizationName(NETA_APP_ORGANIZATION);
    app.setWindowIcon(QIcon(":/resources/images/neta_app.ico"));
    StyleHelper::applyAppPalette(&app);
    app.setStyleSheet(StyleHelper::getAppStyle());

    // Command-line options parsing
    QCommandLineParser parser;
    parser.setApplicationDescription(QString("%1 v%2").arg(NETA_APP_NAME, NETA_APP_VERSION));
    parser.addHelpOption();

    QCommandLineOption hostOption(QStringList() << "host", "Launch in Central Host / Management Server mode.");
    QCommandLineOption clientOption(QStringList() << "client", "Launch in Test Station / Operator mode.");
    QCommandLineOption stationOption(QStringList() << "s" << "station", "Test Workstation Number (1, 2, or 3).", "station_id");
    QCommandLineOption serverOption(QStringList() << "H" << "server" << "host-ip", "Central Host Server IP address (default: 127.0.0.1).", "host_ip");
    QCommandLineOption drxOption(QStringList() << "d" << "drx" << "drx-endpoint", "DRX9000 IP:Port endpoint (e.g. 127.0.0.1:8081).", "endpoint");

    parser.addOption(hostOption);
    parser.addOption(clientOption);
    parser.addOption(stationOption);
    parser.addOption(serverOption);
    parser.addOption(drxOption);
    parser.process(app);

    AppConfig &config = AppConfig::instance();

    if (parser.isSet(stationOption)) {
        config.setStationId(parser.value(stationOption).toInt());
    }

    if (parser.isSet(serverOption)) {
        const QString sIp = parser.value(serverOption).trimmed();
        config.setHostIp(sIp);
        config.setDbHost(sIp);
    } else if (parser.isSet(clientOption)) {
        config.setHostIp("127.0.0.1");
        config.setDbHost("127.0.0.1");
    }

    if (parser.isSet(drxOption)) {
        const QString drxEp = parser.value(drxOption).trimmed();
        if (parser.isSet(hostOption)) {
            config.setRefDrxHost(drxEp);
        } else {
            config.setStationDrxHost(drxEp);
        }
    } else {
        if (parser.isSet(hostOption)) {
            config.setRefDrxHost("127.0.0.1:8080");
        } else if (parser.isSet(clientOption)) {
            int stId = config.stationId();
            if (stId <= 0) stId = 1;
            config.setStationDrxHost(QString("127.0.0.1:%1").arg(8080 + stId));
        }
    }

    if (parser.isSet(hostOption)) {
        config.setRole(AppRole::Host);
        bool connected = DbManager::instance().connectToDatabase(config.dbHost(), config.dbPort(),
                                                                config.dbName(), config.dbUser(),
                                                                config.dbPassword());
        if (!connected && !DependencyInstaller::isMySqlListening(config.dbHost(), config.dbPort())) {
            DependencyInstaller::autoSetupHostEnvironment();
        }
        HostWindow *hostWin = new HostWindow();
        hostWin->showMaximized();
    } else if (parser.isSet(clientOption)) {
        config.setRole(AppRole::Client);
        DbManager::instance().connectToDatabase(config.dbHost(), config.dbPort(),
                                               config.dbName(), config.dbUser(),
                                               config.dbPassword());
        ClientWindow *clientWin = new ClientWindow();
        clientWin->showMaximized();
    } else {
        // 1. Minimalist Login (username & password only)
        LoginDialog loginDlg;
        if (loginDlg.exec() != QDialog::Accepted) {
            return 0;
        }
        config.setUserRole(loginDlg.authenticatedRole());
        config.setCurrentUsername(loginDlg.authenticatedUsername());
        config.setCurrentUserFullName(loginDlg.authenticatedFullName());
        config.save();

        // 2. Unified Multiplayer-Style Lobby (choose HOST server or JOIN active LAN server)
        LobbyWindow *lobbyWin = new LobbyWindow();
        lobbyWin->show();
    }

    return app.exec();
}
