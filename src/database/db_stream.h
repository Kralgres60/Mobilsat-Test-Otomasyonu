/**
 * @file db_stream.h
 * @brief Hardware/engine-agnostic database abstraction layer using function pointers.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DB_STREAM_H
#define DB_STREAM_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

/**
 * @struct DbRow
 * @brief Engine-independent representation of a single database result row.
 */
struct DbRow {
    QVariantList columns;
    QVariantMap  named;

    /** @brief Returns column value by zero-based index. */
    QVariant at(int idx) const {
        return (idx >= 0 && idx < columns.size()) ? columns.at(idx) : QVariant();
    }

    /** @brief Returns column value by column name (case-sensitive). */
    QVariant value(const QString &key) const {
        return named.value(key);
    }

    /** @brief Returns true if the row contains no data. */
    bool isEmpty() const {
        return columns.isEmpty() && named.isEmpty();
    }
};

/**
 * @struct DbQueryResult
 * @brief Engine-independent result of a database query execution.
 */
struct DbQueryResult {
    bool ok = false;
    QString error;
    qint64 lastInsertId = 0;
    int rowsAffected = 0;
    QVector<DbRow> rows;

    /** @brief Returns true if at least one row was returned. */
    bool hasRows() const { return !rows.isEmpty(); }

    /** @brief Returns the first row or an empty DbRow if none. */
    DbRow first() const { return rows.isEmpty() ? DbRow{} : rows.first(); }
};

/**
 * @struct DbDriverOps
 * @brief Function-pointer operation table decoupling DbStream from concrete SQL engines.
 *
 * Any database backend (MariaDB/MySQL, SQLite, PostgreSQL, REST API, or Mock DB)
 * can be plugged into DbStream by populating this struct without modifying business logic.
 */
struct DbDriverOps {
    void *ctx = nullptr;

    bool    (*connect)(void *ctx, const QString &host, int port, const QString &dbName,
                       const QString &user, const QString &password) = nullptr;
    void    (*close)(void *ctx) = nullptr;
    bool    (*isConnected)(void *ctx) = nullptr;
    QString (*driverName)(void *ctx) = nullptr;
    QString (*databaseName)(void *ctx) = nullptr;
    QString (*lastError)(void *ctx) = nullptr;
    bool    (*exec)(void *ctx, const QString &sql,
                    const QVariantList &positionalBinds,
                    const QVariantMap &namedBinds,
                    DbQueryResult *outResult) = nullptr;
    bool    (*beginTransaction)(void *ctx) = nullptr;
    bool    (*commitTransaction)(void *ctx) = nullptr;
    bool    (*rollbackTransaction)(void *ctx) = nullptr;
    bool    (*startLocalService)(void *ctx) = nullptr;
    void    (*stopLocalService)(void *ctx) = nullptr;
};

/**
 * @class DbStream
 * @brief Engine-agnostic database stream dispatching queries via bound DbDriverOps.
 */
class DbStream : public QObject {
    Q_OBJECT
public:
    /** @brief Constructs an unbound DbStream. */
    explicit DbStream(QObject *parent = nullptr);

    /** @brief Constructs a DbStream bound to the given driver operations. */
    explicit DbStream(const DbDriverOps &ops, QObject *parent = nullptr);

    ~DbStream() override = default;

    /** @brief Binds or replaces the underlying database driver operations. */
    void bind(const DbDriverOps &ops);

    /** @brief Returns true if a driver operation table is bound. */
    bool isBound() const;

    /** @brief Opens connection to the database via bound driver. */
    bool connectToDatabase(const QString &host, int port, const QString &dbName,
                           const QString &user, const QString &password);

    /** @brief Closes active database connection. */
    void closeDatabase();

    /** @brief Returns true if database connection is open. */
    bool isConnected() const;

    /** @brief Returns active driver identifier (e.g. "QMYSQL", "QSQLITE"). */
    QString driverName() const;

    /** @brief Returns active database name or file path. */
    QString databaseName() const;

    /** @brief Returns last error string from the bound driver or stream. */
    QString lastError() const;

    /** @brief Executes a plain SQL statement without bindings. */
    DbQueryResult exec(const QString &sql);

    /** @brief Executes a parameterized SQL statement with named (:key) bindings. */
    DbQueryResult execNamed(const QString &sql, const QVariantMap &namedBinds);

    /** @brief Executes a parameterized SQL statement with positional (?) bindings. */
    DbQueryResult execPositional(const QString &sql, const QVariantList &positionalBinds);

    /** @brief Begins a database transaction if supported by the bound driver. */
    bool beginTransaction();

    /** @brief Commits the current database transaction. */
    bool commitTransaction();

    /** @brief Rolls back the current database transaction. */
    bool rollbackTransaction();

    /** @brief Starts an optional local database service (e.g. Portable MariaDB). */
    bool startLocalService();

    /** @brief Stops the optional local database service if running. */
    void stopLocalService();

signals:
    /** @brief Emitted when database connection state changes. */
    void connectionStatusChanged(bool connected, const QString &message);

private:
    DbDriverOps m_ops;
    QString m_lastError;
};

#endif // DB_STREAM_H
