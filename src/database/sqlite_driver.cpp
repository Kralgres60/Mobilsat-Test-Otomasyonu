/**
 * @file sqlite_driver.cpp
 * @brief Implementation of the physical SQLite database driver.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "sqlite_driver.h"
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

namespace {

bool s_connect(void *ctx, const QString &host, int port, const QString &dbName,
               const QString &user, const QString &password) {
    return static_cast<SqliteDriver *>(ctx)->connectToDatabase(host, port, dbName, user, password);
}

void s_close(void *ctx) {
    static_cast<SqliteDriver *>(ctx)->closeDatabase();
}

bool s_isConnected(void *ctx) {
    return static_cast<SqliteDriver *>(ctx)->isConnected();
}

QString s_driverName(void *ctx) {
    return static_cast<SqliteDriver *>(ctx)->driverName();
}

QString s_databaseName(void *ctx) {
    return static_cast<SqliteDriver *>(ctx)->databaseName();
}

QString s_lastError(void *ctx) {
    return static_cast<SqliteDriver *>(ctx)->lastError();
}

bool s_exec(void *ctx, const QString &sql,
            const QVariantList &positionalBinds,
            const QVariantMap &namedBinds,
            DbQueryResult *outResult) {
    return static_cast<SqliteDriver *>(ctx)->exec(sql, positionalBinds, namedBinds, outResult);
}

bool s_beginTx(void *ctx) {
    return static_cast<SqliteDriver *>(ctx)->beginTransaction();
}

bool s_commitTx(void *ctx) {
    return static_cast<SqliteDriver *>(ctx)->commitTransaction();
}

bool s_rollbackTx(void *ctx) {
    return static_cast<SqliteDriver *>(ctx)->rollbackTransaction();
}

} // namespace

SqliteDriver::SqliteDriver(const QString &connectionName, QObject *parent)
    : QObject(parent), m_connectionName(connectionName) {
}

SqliteDriver::~SqliteDriver() {
    closeDatabase();
}

DbDriverOps SqliteDriver::ops() {
    DbDriverOps table;
    table.ctx                 = this;
    table.connect             = &s_connect;
    table.close               = &s_close;
    table.isConnected         = &s_isConnected;
    table.driverName          = &s_driverName;
    table.databaseName        = &s_databaseName;
    table.lastError           = &s_lastError;
    table.exec                = &s_exec;
    table.beginTransaction    = &s_beginTx;
    table.commitTransaction   = &s_commitTx;
    table.rollbackTransaction = &s_rollbackTx;
    return table;
}

bool SqliteDriver::isAvailable() {
    return QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"));
}

bool SqliteDriver::connectToDatabase(const QString &host, int port, const QString &dbName,
                                     const QString &user, const QString &password) {
    Q_UNUSED(host);
    Q_UNUSED(port);
    Q_UNUSED(user);
    Q_UNUSED(password);

    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    const QString filePath = dbName.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)
                                 ? dbName
                                 : QStringLiteral("neta_local_fallback.db");
    db.setDatabaseName(filePath);

    if (!db.open()) {
        m_lastError = db.lastError().text();
        qWarning() << "[SqliteDriver] Failed to open SQLite database:" << m_lastError;
        db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    qDebug() << "[SqliteDriver] Connected to SQLite:" << filePath;
    m_lastError.clear();
    return true;
}

void SqliteDriver::closeDatabase() {
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
}

bool SqliteDriver::isConnected() const {
    if (!QSqlDatabase::contains(m_connectionName)) return false;
    return QSqlDatabase::database(m_connectionName).isOpen();
}

QString SqliteDriver::databaseName() const {
    if (!QSqlDatabase::contains(m_connectionName)) return QString();
    return QSqlDatabase::database(m_connectionName).databaseName();
}

bool SqliteDriver::exec(const QString &sql,
                        const QVariantList &positionalBinds,
                        const QVariantMap &namedBinds,
                        DbQueryResult *outResult) {
    DbQueryResult localRes;
    DbQueryResult *res = outResult ? outResult : &localRes;

    if (!isConnected()) {
        res->ok = false;
        res->error = QStringLiteral("SQLite database is not connected.");
        m_lastError = res->error;
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (positionalBinds.isEmpty() && namedBinds.isEmpty()) {
        if (!q.exec(sql)) {
            res->ok = false;
            res->error = q.lastError().text();
            m_lastError = res->error;
            return false;
        }
    } else {
        if (!q.prepare(sql)) {
            res->ok = false;
            res->error = q.lastError().text();
            m_lastError = res->error;
            return false;
        }
        for (const QVariant &val : positionalBinds) {
            q.addBindValue(val);
        }
        for (auto it = namedBinds.constBegin(); it != namedBinds.constEnd(); ++it) {
            q.bindValue(it.key(), it.value());
        }
        if (!q.exec()) {
            res->ok = false;
            res->error = q.lastError().text();
            m_lastError = res->error;
            return false;
        }
    }

    res->ok = true;
    res->rowsAffected = q.numRowsAffected();
    const QVariant insertIdVar = q.lastInsertId();
    res->lastInsertId = insertIdVar.isValid() ? insertIdVar.toLongLong() : 0;

    const QSqlRecord rec = q.record();
    const int colCount = rec.count();
    if (q.isSelect() || colCount > 0) {
        res->columnNames.reserve(colCount);
        for (int i = 0; i < colCount; ++i) {
            res->columnNames.append(rec.fieldName(i));
        }
        while (q.next()) {
            DbRow row;
            row.columns.reserve(colCount);
            for (int i = 0; i < colCount; ++i) {
                const QVariant val = q.value(i);
                row.columns.append(val);
                row.named.insert(rec.fieldName(i), val);
            }
            res->rows.append(row);
        }
    }

    return true;
}

bool SqliteDriver::beginTransaction() {
    return isConnected() && QSqlDatabase::database(m_connectionName).transaction();
}

bool SqliteDriver::commitTransaction() {
    return isConnected() && QSqlDatabase::database(m_connectionName).commit();
}

bool SqliteDriver::rollbackTransaction() {
    return isConnected() && QSqlDatabase::database(m_connectionName).rollback();
}
