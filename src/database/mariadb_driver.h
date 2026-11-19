/**
 * @file mariadb_driver.h
 * @brief Physical MariaDB/MySQL (and ODBC fallback) database driver and Portable MariaDB service manager.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef MARIADB_DRIVER_H
#define MARIADB_DRIVER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include "db_stream.h"

/**
 * @class MariaDbDriver
 * @brief Physical SQL driver for MariaDB/MySQL (QMYSQL / QODBC) including portable server lifecycle.
 */
class MariaDbDriver : public QObject {
    Q_OBJECT
public:
    /** @brief Constructs a physical MariaDB driver with the given Qt connection name. */
    explicit MariaDbDriver(const QString &connectionName = QStringLiteral("NetaMainDb"),
                           QObject *parent = nullptr);
    ~MariaDbDriver() override;

    /** @brief Returns a DbDriverOps table bound to this MariaDbDriver instance. */
    DbDriverOps ops();

    /** @brief Opens connection to MariaDB/MySQL (with automatic DB provisioning and credential fallback). */
    bool connectToDatabase(const QString &host, int port, const QString &dbName,
                           const QString &user, const QString &password);

    /** @brief Closes active database connection. */
    void closeDatabase();

    /** @brief Returns true if connection is open. */
    bool isConnected() const;

    /** @brief Returns active Qt SQL driver name ("QMYSQL" or "QODBC"). */
    QString driverName() const { return m_driverName; }

    /** @brief Returns active database name. */
    QString databaseName() const;

    /** @brief Returns last SQL error description. */
    QString lastError() const { return m_lastError; }

    /** @brief Executes SQL with optional positional or named bindings and populates outResult. */
    bool exec(const QString &sql,
              const QVariantList &positionalBinds,
              const QVariantMap &namedBinds,
              DbQueryResult *outResult);

    /** @brief Begins a SQL transaction. */
    bool beginTransaction();

    /** @brief Commits the active SQL transaction. */
    bool commitTransaction();

    /** @brief Rolls back the active SQL transaction. */
    bool rollbackTransaction();

    /** @brief Starts local Portable MariaDB process if present on disk. */
    bool startPortableMariaDbIfPresent();

    /** @brief Stops local Portable MariaDB process if launched by this driver. */
    void stopPortableMariaDbIfRunning();

    /** @brief Returns true if QMYSQL or QODBC driver is available in Qt SQL plugins. */
    static bool isAvailable();

private:
    QString m_connectionName;
    QString m_driverName = QStringLiteral("QMYSQL");
    QString m_lastError;
    QString m_appDirPath;
    QProcess *m_portableProcess = nullptr;
    bool m_launchedPortableMysqld = false;
};

#endif // MARIADB_DRIVER_H
