/**
 * @file mariadb_driver.cpp
 * @brief Implementation of the physical MariaDB/MySQL database driver.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "mariadb_driver.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTcpSocket>
#include <QThread>

namespace {

bool s_connect(void *ctx, const QString &host, int port, const QString &dbName,
               const QString &user, const QString &password) {
    return static_cast<MariaDbDriver *>(ctx)->connectToDatabase(host, port, dbName, user, password);
}

void s_close(void *ctx) {
    static_cast<MariaDbDriver *>(ctx)->closeDatabase();
}

bool s_isConnected(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->isConnected();
}

QString s_driverName(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->driverName();
}

QString s_databaseName(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->databaseName();
}

QString s_lastError(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->lastError();
}

bool s_exec(void *ctx, const QString &sql,
            const QVariantList &positionalBinds,
            const QVariantMap &namedBinds,
            DbQueryResult *outResult) {
    return static_cast<MariaDbDriver *>(ctx)->exec(sql, positionalBinds, namedBinds, outResult);
}

bool s_beginTx(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->beginTransaction();
}

bool s_commitTx(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->commitTransaction();
}

bool s_rollbackTx(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->rollbackTransaction();
}

bool s_startLocal(void *ctx) {
    return static_cast<MariaDbDriver *>(ctx)->startPortableMariaDbIfPresent();
}

void s_stopLocal(void *ctx) {
    static_cast<MariaDbDriver *>(ctx)->stopPortableMariaDbIfRunning();
}

} // namespace

MariaDbDriver::MariaDbDriver(const QString &connectionName, QObject *parent)
    : QObject(parent), m_connectionName(connectionName) {
}

MariaDbDriver::~MariaDbDriver() {
    closeDatabase();
    stopPortableMariaDbIfRunning();
}

DbDriverOps MariaDbDriver::ops() {
    DbDriverOps table;
    table.ctx               = this;
    table.connect           = &s_connect;
    table.close             = &s_close;
    table.isConnected       = &s_isConnected;
    table.driverName        = &s_driverName;
    table.databaseName      = &s_databaseName;
    table.lastError         = &s_lastError;
    table.exec              = &s_exec;
    table.beginTransaction  = &s_beginTx;
    table.commitTransaction = &s_commitTx;
    table.rollbackTransaction = &s_rollbackTx;
    table.startLocalService = &s_startLocal;
    table.stopLocalService  = &s_stopLocal;
    return table;
}

bool MariaDbDriver::isAvailable() {
    return QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL")) ||
           QSqlDatabase::isDriverAvailable(QStringLiteral("QODBC"));
}

bool MariaDbDriver::connectToDatabase(const QString &host, int port, const QString &dbName,
                                      const QString &user, const QString &password) {
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    qInfo() << "[MariaDbDriver] Available SQL drivers:" << QSqlDatabase::drivers();

    if (QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL"))) {
        m_driverName = QStringLiteral("QMYSQL");
    } else if (QSqlDatabase::isDriverAvailable(QStringLiteral("QODBC"))) {
        m_driverName = QStringLiteral("QODBC");
    } else {
        m_lastError = QStringLiteral("Neither QMYSQL nor QODBC SQL driver is available.");
        return false;
    }

    const QString effectiveUser = user.isEmpty() ? QStringLiteral("root") : user;
    const QString effectivePassword = password.isEmpty() ? QStringLiteral("neta123") : password;

    QSqlDatabase db = QSqlDatabase::addDatabase(m_driverName, m_connectionName);

    if (m_driverName == QStringLiteral("QMYSQL")) {
        db.setHostName(host);
        db.setPort(port);
        db.setDatabaseName(dbName);
        db.setUserName(effectiveUser);
        db.setPassword(effectivePassword);
        db.setConnectOptions(QStringLiteral("MYSQL_OPT_RECONNECT=1"));
    } else {
        const QString dsn = QStringLiteral("DRIVER={MySQL ODBC 8.0 Driver};SERVER=%1;PORT=%2;DATABASE=%3;UID=%4;PWD=%5;")
                                .arg(host).arg(port).arg(dbName).arg(effectiveUser).arg(effectivePassword);
        db.setDatabaseName(dsn);
    }

    if (!db.open()) {
        m_lastError = db.lastError().text();

        if (m_driverName == QStringLiteral("QMYSQL")) {
            if (m_lastError.contains(QStringLiteral("Access denied"), Qt::CaseInsensitive) ||
                m_lastError.contains(QStringLiteral("auth_gssapi_client"), Qt::CaseInsensitive)) {
                qInfo() << "[MariaDbDriver] Connection rejected; attempting factory credentials (root / neta123)...";
                db.close();
                db.setUserName(QStringLiteral("root"));
                db.setPassword(QStringLiteral("neta123"));
                if (db.open()) {
                    m_lastError.clear();
                } else {
                    db.close();
                    db.setUserName(QStringLiteral("neta"));
                    db.setPassword(QStringLiteral("neta123"));
                    if (db.open()) {
                        m_lastError.clear();
                    }
                }
            }
        }

        if (m_driverName == QStringLiteral("QMYSQL") &&
            (m_lastError.contains(QStringLiteral("Unknown database"), Qt::CaseInsensitive) ||
             db.lastError().nativeErrorCode() == QStringLiteral("1049"))) {
            qInfo() << "[MariaDbDriver] Database" << dbName << "not found; provisioning automatically...";
            db.close();
            db.setDatabaseName(QString());
            if (db.open()) {
                QSqlQuery qCreate(db);
                qCreate.exec(QStringLiteral("CREATE DATABASE IF NOT EXISTS `%1` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;").arg(dbName));
                db.close();
                db.setDatabaseName(dbName);
                if (db.open()) {
                    m_lastError.clear();
                } else {
                    m_lastError = db.lastError().text();
                }
            }
        }

        if (!db.isOpen()) {
            qWarning() << "[MariaDbDriver] Central MariaDB not reachable:" << m_lastError;
            db.close();
            QSqlDatabase::removeDatabase(m_connectionName);
            m_driverName.clear();
            return false;
        }
    }

    qDebug() << QStringLiteral("[MariaDbDriver] Connected to %1:%2 (Driver: %3)")
                    .arg(host).arg(port).arg(m_driverName);
    QSqlQuery qUtf(db);
    qUtf.exec(QStringLiteral("SET NAMES 'utf8mb4'"));
    qUtf.exec(QStringLiteral("SET CHARACTER SET utf8mb4"));
    m_lastError.clear();
    return true;
}

void MariaDbDriver::closeDatabase() {
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
}

bool MariaDbDriver::isConnected() const {
    if (!QSqlDatabase::contains(m_connectionName)) return false;
    return QSqlDatabase::database(m_connectionName).isOpen();
}

QString MariaDbDriver::databaseName() const {
    if (!QSqlDatabase::contains(m_connectionName)) return QString();
    return QSqlDatabase::database(m_connectionName).databaseName();
}

bool MariaDbDriver::exec(const QString &sql,
                         const QVariantList &positionalBinds,
                         const QVariantMap &namedBinds,
                         DbQueryResult *outResult) {
    DbQueryResult localRes;
    DbQueryResult *res = outResult ? outResult : &localRes;

    if (!isConnected()) {
        res->ok = false;
        res->error = QStringLiteral("Database is not connected.");
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

bool MariaDbDriver::beginTransaction() {
    return isConnected() && QSqlDatabase::database(m_connectionName).transaction();
}

bool MariaDbDriver::commitTransaction() {
    return isConnected() && QSqlDatabase::database(m_connectionName).commit();
}

bool MariaDbDriver::rollbackTransaction() {
    return isConnected() && QSqlDatabase::database(m_connectionName).rollback();
}

bool MariaDbDriver::startPortableMariaDbIfPresent() {
    if (QCoreApplication::instance()) {
        m_appDirPath = QCoreApplication::applicationDirPath();
    }
    const QString appDir = m_appDirPath.isEmpty() ? QDir::currentPath() : m_appDirPath;
    const QStringList candidates = {
        appDir + QStringLiteral("/mariadb_portable"),
        appDir + QStringLiteral("/../Database/mariadb_portable"),
        appDir + QStringLiteral("/tools/mariadb_portable"),
        QDir::currentPath() + QStringLiteral("/tools/mariadb_portable"),
        QStringLiteral("D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/mariadb_portable")
    };

    QString portableDir;
    for (const auto &dir : candidates) {
        if (QFile::exists(dir + QStringLiteral("/bin/mysqld.exe"))) {
            portableDir = QDir::cleanPath(dir);
            break;
        }
    }

    if (portableDir.isEmpty()) {
        qWarning() << "[MariaDbDriver] Portable MariaDB directory not found.";
        return false;
    }

    qInfo() << "[MariaDbDriver] Portable MariaDB found at:" << portableDir;
    const QString mysqldPath = portableDir + QStringLiteral("/bin/mysqld.exe");
    const QString iniPath = portableDir + QStringLiteral("/my.ini");
    const QString dataPath = portableDir + QStringLiteral("/data");
    const QString installDbPath = portableDir + QStringLiteral("/bin/mysql_install_db.exe");

    if (!QDir(dataPath + QStringLiteral("/mysql")).exists() && QFile::exists(installDbPath)) {
        qInfo() << "[MariaDbDriver] Initializing clean portable data directory with mysql_install_db...";
        QDir d(dataPath);
        if (d.exists()) {
            const QFileInfoList entries = d.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
            for (const QFileInfo &fi : entries) {
                if (fi.isDir()) {
                    QDir(fi.absoluteFilePath()).removeRecursively();
                } else {
                    QFile::remove(fi.absoluteFilePath());
                }
            }
        } else {
            d.mkpath(QStringLiteral("."));
        }

        QStringList initArgs;
        initArgs << QStringLiteral("--datadir=%1").arg(dataPath)
                 << QStringLiteral("--password=neta123")
                 << QStringLiteral("--allow-remote-root-access")
                 << QStringLiteral("--port=3306")
                 << QStringLiteral("--silent");
        QProcess::execute(installDbPath, initArgs);
    }

    if (!m_portableProcess) {
        m_portableProcess = new QProcess(this);
    }

    if (m_portableProcess->state() == QProcess::NotRunning) {
        QStringList args;
        args << QStringLiteral("--defaults-file=%1").arg(iniPath);
        args << QStringLiteral("--datadir=%1").arg(dataPath);

        m_portableProcess->start(mysqldPath, args);
        if (!m_portableProcess->waitForStarted(3000)) {
            qWarning() << "[MariaDbDriver] Failed to launch portable MariaDB:" << m_portableProcess->errorString();
            return false;
        }

        m_launchedPortableMysqld = true;
        qInfo() << "[MariaDbDriver] Portable MariaDB launched in background (PID:" << m_portableProcess->processId() << ")";

        QElapsedTimer waitTimer;
        waitTimer.start();
        while (waitTimer.elapsed() < 6000) {
            QTcpSocket probe;
            probe.connectToHost(QStringLiteral("127.0.0.1"), 3306);
            if (probe.waitForConnected(120)) {
                probe.disconnectFromHost();
                break;
            }
            QThread::msleep(100);
        }
    }
    return true;
}

void MariaDbDriver::stopPortableMariaDbIfRunning() {
    if (m_launchedPortableMysqld && m_portableProcess && m_portableProcess->state() != QProcess::NotRunning) {
        qInfo() << "[MariaDbDriver] Stopping local portable MariaDB process...";
        const QString appDir = m_appDirPath.isEmpty() ? QDir::currentPath() : m_appDirPath;
        const QStringList candidates = {
            appDir + QStringLiteral("/mariadb_portable/bin/mysqladmin.exe"),
            appDir + QStringLiteral("/../Database/mariadb_portable/bin/mysqladmin.exe"),
            appDir + QStringLiteral("/tools/mariadb_portable/bin/mysqladmin.exe"),
            QDir::currentPath() + QStringLiteral("/tools/mariadb_portable/bin/mysqladmin.exe"),
            QStringLiteral("D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/mariadb_portable/bin/mysqladmin.exe")
        };
        for (const auto &admin : candidates) {
            if (QFile::exists(admin)) {
                QProcess::execute(admin, QStringList() << QStringLiteral("-h") << QStringLiteral("127.0.0.1")
                                                       << QStringLiteral("-P") << QStringLiteral("3306")
                                                       << QStringLiteral("-u") << QStringLiteral("root")
                                                       << QStringLiteral("-pneta123") << QStringLiteral("shutdown"));
                break;
            }
        }
        if (!m_portableProcess->waitForFinished(4000)) {
            m_portableProcess->kill();
        }
        m_launchedPortableMysqld = false;
    }
}
