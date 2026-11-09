/**
 * @file dependency_installer.cpp
 * @brief Implementation of automated MariaDB dependency management.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "dependency_installer.h"
#include "database/db_manager.h"
#include "core/app_config.h"
#include "ui/common/dependency_install_dialog.h"
#include <QTcpSocket>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

bool DependencyInstaller::isMySqlListening(const QString &host, int port) {
    QTcpSocket socket;
    socket.connectToHost(host, port);
    bool connected = socket.waitForConnected(1200);
    if (connected) {
        socket.disconnectFromHost();
    }
    return connected;
}

bool DependencyInstaller::isDatabaseConfigured() {
    return DbManager::instance().isConnected();
}

bool DependencyInstaller::isRunningAsAdministrator() {
#ifdef Q_OS_WIN
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return (isAdmin == TRUE);
#else
    return false;
#endif
}

QString DependencyInstaller::findInstallerScript() {
    QStringList candidates = {
        QDir::current().filePath("tools/setup_mysql/install_mysql_auto.bat"),
        QDir::current().filePath("setup_mysql.bat"),
        QCoreApplication::applicationDirPath() + "/tools/setup_mysql/install_mysql_auto.bat",
        QCoreApplication::applicationDirPath() + "/setup_mysql.bat",
        QCoreApplication::applicationDirPath() + "/../tools/setup_mysql/install_mysql_auto.bat",
        QCoreApplication::applicationDirPath() + "/../../tools/setup_mysql/install_mysql_auto.bat",
        "D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/setup_mysql/install_mysql_auto.bat"
    };

    for (const auto &p : candidates) {
        QFileInfo fi(p);
        if (fi.exists() && fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }
    return QString();
}

QString DependencyInstaller::findPsInstallerScript() {
    QStringList candidates = {
        QDir::current().filePath("tools/setup_mysql/install_mysql_auto.ps1"),
        QCoreApplication::applicationDirPath() + "/tools/setup_mysql/install_mysql_auto.ps1",
        QCoreApplication::applicationDirPath() + "/../tools/setup_mysql/install_mysql_auto.ps1",
        QCoreApplication::applicationDirPath() + "/../../tools/setup_mysql/install_mysql_auto.ps1",
        "D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/setup_mysql/install_mysql_auto.ps1"
    };

    for (const auto &p : candidates) {
        QFileInfo fi(p);
        if (fi.exists() && fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }
    return QString();
}

QString DependencyInstaller::findResetScript() {
    QStringList candidates = {
        QDir::current().filePath("tools/setup_mysql/reset_database.bat"),
        QCoreApplication::applicationDirPath() + "/tools/setup_mysql/reset_database.bat",
        QCoreApplication::applicationDirPath() + "/../tools/setup_mysql/reset_database.bat",
        QCoreApplication::applicationDirPath() + "/../../tools/setup_mysql/reset_database.bat",
        "D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/setup_mysql/reset_database.bat"
    };

    for (const auto &p : candidates) {
        QFileInfo fi(p);
        if (fi.exists() && fi.isFile()) {
            return fi.absoluteFilePath();
        }
    }
    return QString();
}

bool DependencyInstaller::runInstallerWithElevation(QWidget *parent, bool /*askConfirmation*/) {
    DependencyInstallDialog dlg(parent);
    int res = dlg.exec();
    return (res == QDialog::Accepted && dlg.isSuccess());
}

bool DependencyInstaller::autoSetupHostEnvironment(QWidget *parent) {
    AppConfig &cfg = AppConfig::instance();

    // 1. If MySQL is already listening on port 3306, attempt immediate database connection
    if (isMySqlListening("127.0.0.1", 3306)) {
        bool connected = DbManager::instance().connectToDatabase(
            cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword()
        );
        if (connected) {
            DbManager::instance().ensureDefaultDataIfMissing();
            return true;
        }
    }

    // 2. MySQL is not running or unconfigured: Launch interactive automated installer dialog
    DependencyInstallDialog dlg(parent);
    int res = dlg.exec();
    return (res == QDialog::Accepted && dlg.isSuccess());
}

bool DependencyInstaller::runResetWithElevation(QWidget *parent) {
    QString script = findResetScript();
    if (script.isEmpty()) {
        QMessageBox::critical(parent, "Error", "Database reset script not found!");
        return false;
    }

    QDir workDir = QFileInfo(script).dir();

#ifdef Q_OS_WIN
    HINSTANCE hInst = ShellExecuteW(
        nullptr,
        L"runas",
        reinterpret_cast<LPCWSTR>(script.utf16()),
        nullptr,
        reinterpret_cast<LPCWSTR>(workDir.absolutePath().utf16()),
        SW_SHOWNORMAL
    );

    return (reinterpret_cast<INT_PTR>(hInst) > 32);
#else
    return false;
#endif
}
