/**
 * @file backup_manager.h
 * @brief Automated and manual database backup management and pruning service.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QStringList>

/**
 * @class BackupManager
 * @brief Singleton service executing scheduled and manual database backups for SQLite and MariaDB,
 *        with automatic retention pruning (e.g. cleaning backups older than 30 days).
 */
class BackupManager : public QObject {
    Q_OBJECT
public:
    /** @brief Returns global BackupManager instance. */
    static BackupManager& instance();

    /**
     * @brief Starts periodic auto-backup timer.
     * @param intervalHours Periodic interval in hours (default: 24).
     */
    void startAutoBackup(int intervalHours = 24);

    /** @brief Stops the periodic auto-backup timer. */
    void stopAutoBackup();

    /** @brief Checks if auto-backup timer is currently running. */
    bool isAutoBackupEnabled() const;

    /** @brief Returns the configured auto-backup interval in hours. */
    int autoBackupIntervalHours() const { return m_intervalHours; }

    /**
     * @brief Executes a backup of the active database (SQLite or MariaDB).
     * @param outFilePath Optional pointer to receive output backup file path.
     * @param outError Optional pointer to receive error description on failure.
     * @return true on success, false on failure.
     */
    bool performBackup(QString *outFilePath = nullptr, QString *outError = nullptr);

    /**
     * @brief Prunes backup files older than maxAgeDays.
     * @param maxAgeDays Maximum retention period in days (default: 30).
     * @return Number of deleted backup files.
     */
    int pruneOldBackups(int maxAgeDays = 30);

    /** @brief Returns the absolute path of the backup storage directory. */
    QString backupDirectory() const;

    /** @brief Sets custom backup storage directory. */
    void setBackupDirectory(const QString &dirPath);

    /** @brief Returns timestamp string of the last successful backup. */
    QString lastBackupTime() const { return m_lastBackupTime; }

    /** @brief Returns file path of the last successful backup. */
    QString lastBackupPath() const { return m_lastBackupPath; }

    /** @brief Returns file list of existing backups sorted newest first. */
    QFileInfoList listBackups() const;

signals:
    /** @brief Emitted when a backup operation begins. */
    void backupStarted();

    /**
     * @brief Emitted when a backup completes successfully.
     * @param filePath Absolute path of generated backup file.
     * @param sizeBytes Size of backup file in bytes.
     */
    void backupCompleted(const QString &filePath, qint64 sizeBytes);

    /**
     * @brief Emitted when a backup operation fails.
     * @param error Description of failure.
     */
    void backupFailed(const QString &error);

    /**
     * @brief Emitted when old backups are pruned.
     * @param count Number of pruned files.
     */
    void backupsPruned(int count);

private slots:
    void onAutoBackupTimer();

private:
    explicit BackupManager(QObject *parent = nullptr);
    ~BackupManager() override;
    BackupManager(const BackupManager&) = delete;
    BackupManager& operator=(const BackupManager&) = delete;

    bool backupSqlite(const QString &destPath, QString *outError);
    bool backupMariaDb(const QString &destPath, QString *outError);
    bool dumpMariaDbNative(const QString &destPath, QString *outError);
    bool dumpMariaDbMysqldump(const QString &mysqldumpPath, const QString &destPath, QString *outError);

    QTimer *m_timer = nullptr;
    int m_intervalHours = 24;
    QString m_backupDir;
    QString m_lastBackupTime;
    QString m_lastBackupPath;
};

#endif // BACKUP_MANAGER_H
