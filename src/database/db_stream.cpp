/**
 * @file db_stream.cpp
 * @brief Implementation of the engine-agnostic DbStream abstraction layer.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "db_stream.h"

DbStream::DbStream(QObject *parent) : QObject(parent) {
}

DbStream::DbStream(const DbDriverOps &ops, QObject *parent)
    : QObject(parent), m_ops(ops) {
}

void DbStream::bind(const DbDriverOps &ops) {
    m_ops = ops;
}

bool DbStream::isBound() const {
    return m_ops.exec != nullptr && m_ops.connect != nullptr;
}

bool DbStream::connectToDatabase(const QString &host, int port, const QString &dbName,
                                 const QString &user, const QString &password) {
    if (!m_ops.connect) {
        m_lastError = QStringLiteral("DbStream: no driver bound.");
        emit connectionStatusChanged(false, m_lastError);
        return false;
    }
    const bool ok = m_ops.connect(m_ops.ctx, host, port, dbName, user, password);
    if (!ok) {
        m_lastError = m_ops.lastError ? m_ops.lastError(m_ops.ctx) : QStringLiteral("Connection failed");
        emit connectionStatusChanged(false, m_lastError);
        return false;
    }
    m_lastError.clear();
    emit connectionStatusChanged(true, QStringLiteral("Connected (%1)").arg(driverName()));
    return true;
}

void DbStream::closeDatabase() {
    if (m_ops.close) {
        m_ops.close(m_ops.ctx);
    }
}

bool DbStream::isConnected() const {
    return m_ops.isConnected ? m_ops.isConnected(m_ops.ctx) : false;
}

QString DbStream::driverName() const {
    return m_ops.driverName ? m_ops.driverName(m_ops.ctx) : QString();
}

QString DbStream::databaseName() const {
    return m_ops.databaseName ? m_ops.databaseName(m_ops.ctx) : QString();
}

QString DbStream::lastError() const {
    if (m_ops.lastError) {
        const QString drvErr = m_ops.lastError(m_ops.ctx);
        if (!drvErr.isEmpty()) return drvErr;
    }
    return m_lastError;
}

DbQueryResult DbStream::exec(const QString &sql) {
    return execPositional(sql, QVariantList{});
}

DbQueryResult DbStream::execNamed(const QString &sql, const QVariantMap &namedBinds) {
    DbQueryResult res;
    if (!m_ops.exec) {
        res.error = QStringLiteral("DbStream: no exec handler bound.");
        m_lastError = res.error;
        return res;
    }
    m_ops.exec(m_ops.ctx, sql, QVariantList{}, namedBinds, &res);
    if (!res.ok) {
        m_lastError = res.error;
    }
    return res;
}

DbQueryResult DbStream::execPositional(const QString &sql, const QVariantList &positionalBinds) {
    DbQueryResult res;
    if (!m_ops.exec) {
        res.error = QStringLiteral("DbStream: no exec handler bound.");
        m_lastError = res.error;
        return res;
    }
    m_ops.exec(m_ops.ctx, sql, positionalBinds, QVariantMap{}, &res);
    if (!res.ok) {
        m_lastError = res.error;
    }
    return res;
}

bool DbStream::beginTransaction() {
    return m_ops.beginTransaction ? m_ops.beginTransaction(m_ops.ctx) : false;
}

bool DbStream::commitTransaction() {
    return m_ops.commitTransaction ? m_ops.commitTransaction(m_ops.ctx) : false;
}

bool DbStream::rollbackTransaction() {
    return m_ops.rollbackTransaction ? m_ops.rollbackTransaction(m_ops.ctx) : false;
}

bool DbStream::startLocalService() {
    return m_ops.startLocalService ? m_ops.startLocalService(m_ops.ctx) : false;
}

void DbStream::stopLocalService() {
    if (m_ops.stopLocalService) {
        m_ops.stopLocalService(m_ops.ctx);
    }
}
