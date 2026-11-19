/**
 * @file sqlite_driver.h
 * @brief Physical SQLite database driver for local standalone / fallback operation.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SQLITE_DRIVER_H
#define SQLITE_DRIVER_H

#include <QObject>
#include <QString>
#include "db_stream.h"

/**
 * @class SqliteDriver
 * @brief Physical SQL driver for embedded SQLite databases (QSQLITE).
 */
class SqliteDriver : public QObject {
    Q_OBJECT
public:
    /** @brief Constructs a physical SQLite driver with the given Qt connection name. */
    explicit SqliteDriver(const QString &connectionName = QStringLiteral("NetaMainDb"),
                          QObject *parent = nullptr);
    ~SqliteDriver() override;

    /** @brief Returns a DbDriverOps table bound to this SqliteDriver instance. */
    DbDriverOps ops();

    /** @brief Opens the SQLite database file (defaults to neta_local_fallback.db if dbName is empty). */
    bool connectToDatabase(const QString &host, int port, const QString &dbName,
                           const QString &user, const QString &password);

    /** @brief Closes active SQLite connection. */
    void closeDatabase();

    /** @brief Returns true if connection is open. */
    bool isConnected() const;

    /** @brief Returns "QSQLITE". */
    QString driverName() const { return QStringLiteral("QSQLITE"); }

    /** @brief Returns active SQLite file path. */
    QString databaseName() const;

    /** @brief Returns last SQL error description. */
    QString lastError() const { return m_lastError; }

    /** @brief Executes SQL with optional positional or named bindings and populates outResult. */
    bool exec(const QString &sql,
              const QVariantList &positionalBinds,
              const QVariantMap &namedBinds,
              DbQueryResult *outResult);

    /** @brief Begins a SQLite transaction. */
    bool beginTransaction();

    /** @brief Commits the active SQLite transaction. */
    bool commitTransaction();

    /** @brief Rolls back the active SQLite transaction. */
    bool rollbackTransaction();

    /** @brief Returns true if QSQLITE driver is available in Qt SQL plugins. */
    static bool isAvailable();

private:
    QString m_connectionName;
    QString m_lastError;
};

#endif // SQLITE_DRIVER_H
