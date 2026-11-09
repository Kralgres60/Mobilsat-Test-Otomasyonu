/**
 * @file dependency_installer.h
 * @brief Automated MariaDB database installer, service supervisor, and environment readiness check.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DEPENDENCY_INSTALLER_H
#define DEPENDENCY_INSTALLER_H

#include <QObject>
#include <QString>
#include <QWidget>

/**
 * @class DependencyInstaller
 * @brief Manages prerequisites verification and automated silent installation of MariaDB for Host servers.
 */
class DependencyInstaller : public QObject {
    Q_OBJECT
public:
    /** @brief Checks whether a database server is actively listening on the target TCP port. */
    static bool isMySqlListening(const QString &host = "127.0.0.1", int port = 3306);

    /** @brief Checks whether the factory schema and tables are initialized and reachable. */
    static bool isDatabaseConfigured();

    /** @brief Verifies whether the application process currently possesses administrative privileges. */
    static bool isRunningAsAdministrator();

    /** @brief Locates automated batch installation script on disk. */
    static QString findInstallerScript();

    /** @brief Locates automated PowerShell installation script on disk. */
    static QString findPsInstallerScript();

    /** @brief Locates database service reset script on disk. */
    static QString findResetScript();

    /** @brief Runs silent MariaDB installer with Windows UAC privilege elevation. */
    static bool runInstallerWithElevation(QWidget *parent = nullptr, bool askConfirmation = true);

    /** @brief Runs database service reset with UAC elevation. */
    static bool runResetWithElevation(QWidget *parent = nullptr);

    /** @brief Validates host environment, performing automated unattended setup if required. */
    static bool autoSetupHostEnvironment(QWidget *parent = nullptr);
};

#endif // DEPENDENCY_INSTALLER_H
