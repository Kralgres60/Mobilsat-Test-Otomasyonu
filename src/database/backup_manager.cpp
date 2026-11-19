/**
 * @file backup_manager.cpp
 * @brief Automated and manual database backup management and pruning service.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "backup_manager.h"
#include "db_manager.h"
#include "db_stream.h"
#include "core/app_config.h"
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

BackupManager& BackupManager::instance() {
    static BackupManager s_instance;
    return s_instance;
}

BackupManager::BackupManager(QObject *parent)
    : QObject(parent),
      m_timer(new QTimer(this)),
      m_intervalHours(24) {
    QString baseDir = QCoreApplication::applicationDirPath();
    if (baseDir.isEmpty()) {
        baseDir = QDir::currentPath();
    }
    m_backupDir = baseDir + "/backups";
    QDir().mkpath(m_backupDir);

    connect(m_timer, &QTimer::timeout, this, &BackupManager::onAutoBackupTimer);
}

BackupManager::~BackupManager() {
    stopAutoBackup();
}

void BackupManager::startAutoBackup(int intervalHours) {
    m_intervalHours = qMax(1, intervalHours);
    if (!m_timer->isActive()) {
        // Start periodic interval (hours -> milliseconds)
        qint64 intervalMs = static_cast<qint64>(m_intervalHours) * 3600 * 1000;
        m_timer->start(static_cast<int>(intervalMs));
        qInfo() << "[BackupManager] Auto-backup timer started with interval:" << m_intervalHours << "hours.";
    }
}

void BackupManager::stopAutoBackup() {
    if (m_timer->isActive()) {
        m_timer->stop();
        qInfo() << "[BackupManager] Auto-backup timer stopped.";
    }
}

bool BackupManager::isAutoBackupEnabled() const {
    return m_timer && m_timer->isActive();
}

QString BackupManager::backupDirectory() const {
    return m_backupDir;
}

void BackupManager::setBackupDirectory(const QString &dirPath) {
    if (!dirPath.isEmpty()) {
        m_backupDir = dirPath;
        QDir().mkpath(m_backupDir);
    }
}

QFileInfoList BackupManager::listBackups() const {
    QDir dir(m_backupDir);
    QStringList filters;
    filters << "neta_backup_*.db" << "neta_backup_*.sql" << "neta_backup_*.bak";
    return dir.entryInfoList(filters, QDir::Files, QDir::Time);
}

int BackupManager::pruneOldBackups(int maxAgeDays) {
    if (maxAgeDays <= 0) return 0;

    QFileInfoList files = listBackups();
    QDateTime now = QDateTime::currentDateTime();
    int prunedCount = 0;

    for (const QFileInfo &fi : files) {
        qint64 daysOld = fi.lastModified().daysTo(now);
        if (daysOld > maxAgeDays) {
            QString path = fi.absoluteFilePath();
            if (QFile::remove(path)) {
                prunedCount++;
                qInfo() << "[BackupManager] Pruned old backup:" << fi.fileName()
                        << "(Age:" << daysOld << "days)";
            }
        }
    }

    if (prunedCount > 0) {
        emit backupsPruned(prunedCount);
    }
    return prunedCount;
}

void BackupManager::onAutoBackupTimer() {
    qInfo() << "[BackupManager] Periodic auto-backup triggered by timer.";
    QString outPath;
    QString outErr;
    if (performBackup(&outPath, &outErr)) {
        qInfo() << "[BackupManager] Scheduled backup succeeded:" << outPath;
    } else {
        qWarning() << "[BackupManager] Scheduled backup failed:" << outErr;
    }
}

bool BackupManager::performBackup(QString *outFilePath, QString *outError) {
    emit backupStarted();

    if (!DbManager::instance().isConnected()) {
        QString err = "Veritabanı bağlı değil, yedekleme başlatılamadı.";
        if (outError) *outError = err;
        emit backupFailed(err);
        return false;
    }

    QDir dir(m_backupDir);
    if (!dir.exists()) {
        dir.mkpath(m_backupDir);
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    bool success = false;
    QString targetFile;

    if (DbManager::instance().isUsingSqlite()) {
        targetFile = dir.filePath(QString("neta_backup_sqlite_%1.db").arg(timestamp));
        success = backupSqlite(targetFile, outError);
    } else {
        targetFile = dir.filePath(QString("neta_backup_mariadb_%1.sql").arg(timestamp));
        success = backupMariaDb(targetFile, outError);
    }

    if (success) {
        QFileInfo fi(targetFile);
        qint64 size = fi.size();
        m_lastBackupTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        m_lastBackupPath = targetFile;

        if (outFilePath) *outFilePath = targetFile;

        // Automatically prune backups older than 30 days
        pruneOldBackups(30);

        // Record audit trail
        DbManager::instance().logAudit("DB_BACKUP",
            QString("Backup file generated: %1 (%2 KB)")
                .arg(fi.fileName())
                .arg(size / 1024),
            AppConfig::instance().currentUsername());

        emit backupCompleted(targetFile, size);
        return true;
    }

    emit backupFailed(outError ? *outError : "Bilinmeyen yedekleme hatası");
    return false;
}

bool BackupManager::backupSqlite(const QString &destPath, QString *outError) {
    DbStream *stream = DbManager::instance().stream();
    if (!stream || !stream->isConnected()) {
        if (outError) *outError = "SQLite veritabanı açık değil.";
        return false;
    }

    // Try modern SQLite VACUUM INTO for atomic online non-blocking backup
    const QString vacuumSql = QString("VACUUM INTO '%1'").arg(destPath);
    if (stream->exec(vacuumSql).ok) {
        qInfo() << "[BackupManager] SQLite VACUUM INTO backup succeeded:" << destPath;
        return true;
    }

    // Fallback: Safe file copy
    const QString sourceFile = stream->databaseName();
    if (sourceFile.isEmpty() || !QFile::exists(sourceFile)) {
        if (outError) *outError = QString("Kaynak SQLite dosyası bulunamadı: %1").arg(sourceFile);
        return false;
    }

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    if (!QFile::copy(sourceFile, destPath)) {
        if (outError) *outError = QString("SQLite dosyası kopyalanamadı: %1").arg(destPath);
        return false;
    }

    qInfo() << "[BackupManager] SQLite file copy backup succeeded:" << destPath;
    return true;
}

bool BackupManager::backupMariaDb(const QString &destPath, QString *outError) {
    // 1. Search for mysqldump executable
    QString baseDir = QCoreApplication::applicationDirPath();
    if (baseDir.isEmpty()) baseDir = QDir::currentPath();

    QStringList candidates = {
        baseDir + "/tools/mariadb_portable/bin/mysqldump.exe",
        baseDir + "/mariadb_portable/bin/mysqldump.exe",
        "C:/Program Files/MariaDB/bin/mysqldump.exe",
        "C:/Program Files/MySQL/MySQL Server 8.0/bin/mysqldump.exe",
        "C:/Program Files/MySQL/MySQL Server 8.4/bin/mysqldump.exe"
    };

    QString foundDump;
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            foundDump = path;
            break;
        }
    }

    if (foundDump.isEmpty()) {
        foundDump = QStandardPaths::findExecutable("mysqldump");
    }

    if (!foundDump.isEmpty()) {
        if (dumpMariaDbMysqldump(foundDump, destPath, outError)) {
            return true;
        }
        qWarning() << "[BackupManager] mysqldump tool failed, falling back to native SQL export:"
                   << (outError ? *outError : "");
    }

    // 2. Fallback to native C++ SQL table dumper
    return dumpMariaDbNative(destPath, outError);
}

bool BackupManager::dumpMariaDbMysqldump(const QString &mysqldumpPath, const QString &destPath, QString *outError) {
    AppConfig &cfg = AppConfig::instance();
    QProcess process;
    QStringList args;

    args << QString("-h%1").arg(cfg.dbHost().isEmpty() ? "127.0.0.1" : cfg.dbHost());
    args << QString("-P%1").arg(cfg.dbPort() > 0 ? cfg.dbPort() : 3306);
    args << QString("-u%1").arg(cfg.dbUser().isEmpty() ? "root" : cfg.dbUser());

    if (!cfg.dbPassword().isEmpty()) {
        args << QString("-p%1").arg(cfg.dbPassword());
    }

    args << "--single-transaction";
    args << "--quick";
    args << QString("--result-file=%1").arg(destPath);
    args << (cfg.dbName().isEmpty() ? "neta_antenna_test" : cfg.dbName());

    process.start(mysqldumpPath, args);
    if (!process.waitForStarted(5000)) {
        if (outError) *outError = "mysqldump işlemi başlatılamadı.";
        return false;
    }

    if (!process.waitForFinished(60000)) {
        process.kill();
        if (outError) *outError = "mysqldump zaman aşımına uğradı (60 sn).";
        return false;
    }

    if (process.exitCode() != 0) {
        QString errText = QString::fromUtf8(process.readAllStandardError());
        if (outError) *outError = QString("mysqldump hata kodu %1: %2").arg(process.exitCode()).arg(errText);
        return false;
    }

    qInfo() << "[BackupManager] mysqldump executed successfully:" << destPath;
    return true;
}

bool BackupManager::dumpMariaDbNative(const QString &destPath, QString *outError) {
    DbStream *stream = DbManager::instance().stream();
    if (!stream || !stream->isConnected()) {
        if (outError) *outError = "MariaDB veritabanı açık değil.";
        return false;
    }

    QFile file(destPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (outError) *outError = QString("Yedek dosyası açılamadı: %1").arg(destPath);
        return false;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#else
    out.setEncoding(QStringConverter::Utf8);
#endif

    // SQL Header
    out << "-- ========================================================\n";
    out << "-- NETA DRX9000 Antenna Test Native MariaDB Backup Dump\n";
    out << "-- Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    out << "-- Target File: " << QFileInfo(destPath).fileName() << "\n";
    out << "-- ========================================================\n\n";
    out << "SET FOREIGN_KEY_CHECKS = 0;\n";
    out << "SET SQL_MODE = 'NO_AUTO_VALUE_ON_ZERO';\n\n";

    // Enumerate tables
    const DbQueryResult tableRes = stream->exec("SHOW FULL TABLES WHERE Table_type = 'BASE TABLE'");
    if (!tableRes.ok) {
        if (outError) *outError = QString("Tablolar listelenemedi: %1").arg(tableRes.error);
        file.close();
        return false;
    }

    QStringList tables;
    for (const DbRow &row : tableRes.rows) {
        tables << row.at(0).toString();
    }

    for (const QString &tableName : tables) {
        out << "--\n-- Structure for table `" << tableName << "`\n--\n";
        out << "DROP TABLE IF EXISTS `" << tableName << "`;\n";

        const DbQueryResult createRes = stream->exec(QString("SHOW CREATE TABLE `%1`").arg(tableName));
        if (createRes.ok && createRes.hasRows()) {
            out << createRes.first().at(1).toString() << ";\n\n";
        }

        // Dump data
        out << "--\n-- Dumping data for table `" << tableName << "`\n--\n";
        const DbQueryResult dataRes = stream->exec(QString("SELECT * FROM `%1`").arg(tableName));
        if (dataRes.ok) {
            QStringList insertRows;
            int batchCount = 0;

            for (const DbRow &row : dataRes.rows) {
                const int colCount = row.columns.size();
                QStringList rowValues;
                for (int i = 0; i < colCount; ++i) {
                    const QVariant &val = row.at(i);
                    if (val.isNull()) {
                        rowValues << "NULL";
                    } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                        int tid = val.typeId();
                        bool isNumeric = (tid == QMetaType::Int || tid == QMetaType::LongLong ||
                                          tid == QMetaType::ULongLong || tid == QMetaType::Double);
#else
                        QVariant::Type t = val.type();
                        bool isNumeric = (t == QVariant::Int || t == QVariant::LongLong ||
                                          t == QVariant::ULongLong || t == QVariant::Double);
#endif
                        if (isNumeric) {
                            rowValues << val.toString();
                        } else {
                            QString str = val.toString();
                            str.replace("\\", "\\\\");
                            str.replace("'", "\\'");
                            str.replace("\n", "\\n");
                            str.replace("\r", "\\r");
                            rowValues << QString("'%1'").arg(str);
                        }
                    }
                }

                insertRows << QString("(%1)").arg(rowValues.join(", "));
                batchCount++;

                // Flush in batches of 100 rows
                if (batchCount >= 100) {
                    out << QString("INSERT INTO `%1` VALUES\n  %2;\n")
                               .arg(tableName)
                               .arg(insertRows.join(",\n  "));
                    insertRows.clear();
                    batchCount = 0;
                }
            }

            if (!insertRows.isEmpty()) {
                out << QString("INSERT INTO `%1` VALUES\n  %2;\n\n")
                           .arg(tableName)
                           .arg(insertRows.join(",\n  "));
            }
        }
        out << "\n";
    }

    out << "SET FOREIGN_KEY_CHECKS = 1;\n";
    out << "-- Dump completed on " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";

    file.flush();
    file.close();

    qInfo() << "[BackupManager] Native MariaDB SQL backup completed:" << destPath;
    return true;
}
