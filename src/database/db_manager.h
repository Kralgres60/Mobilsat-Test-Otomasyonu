/**
 * @file db_manager.h
 * @brief Thread-safe database management interface for MariaDB/MySQL storage and atomic distributed locking.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMap>
#include <QProcess>
#include <QThread>
#include "data_models.h"

/**
 * @class DbManager
 * @brief Singleton managing database connectivity, automatic schema provisioning,
 *        test run persistence, work orders, satellite profiles, and distributed atomic locking.
 */
class DbManager : public QObject {
    Q_OBJECT
public:
    /** @brief Returns global DbManager instance. */
    static DbManager& instance();

    /**
     * @brief Establishes database connection with specified credentials.
     * @param host Database host IP or hostname
     * @param port TCP port (default 3306)
     * @param dbName Target database name
     * @param user SQL user
     * @param password SQL password
     * @return true on success; false on failure
     */
    bool connectToDatabase(const QString &host, int port, const QString &dbName,
                           const QString &user, const QString &password);

    /** @brief Closes active database connection. */
    void closeDatabase();

    /** @brief Checks if database is currently connected and open. */
    bool isConnected() const;

    /**
     * @brief Automatically launches local Portable MariaDB instance if available on disk.
     * @return true if running or started successfully; false otherwise.
     */
    bool startPortableMariaDbIfPresent();

    /** @brief Stops the local Portable MariaDB instance if it was launched by this process. */
    void stopPortableMariaDbIfRunning();

    /** @brief Returns last SQL error description. */
    QString lastError() const { return m_lastError; }

    /** @brief Returns active Qt SQL driver name (e.g. QMYSQL). */
    QString driverName() const { return m_driverName; }

    // Automatic Schema & Default Seed Data Provisioning
    bool ensureDefaultDataIfMissing();
    bool executeSqlScript(const QString &resourceOrFilePath);

    // Profile Management
    QVector<SatelliteProfileInfo> getProfiles();
    SatelliteProfileInfo getProfile(int profileId);
    bool saveProfile(const SatelliteProfileInfo &profile);

    // Work Order Management
    QVector<WorkOrderInfo> getActiveWorkOrders();
    bool saveWorkOrder(const WorkOrderInfo &order);
    bool incrementOrderCount(int workOrderId, bool isPass);

    // Test Run History & Reporting
    qint64 insertTestRun(const TestRunData &run);
    QVector<TestRunData> getTestRuns(int limit = 100, const QString &searchSn = QString());

    // System Settings Key-Value Store
    QString getSetting(const QString &key, const QString &defaultValue = QString());
    bool setSetting(const QString &key, const QString &value, const QString &desc = QString());

    // User Authentication & Roles
    /**
     * @brief Authenticates a user against database or factory defaults.
     * @param username Login username
     * @param password Plaintext password
     * @param outRole Output UserRole (Technician or Engineer)
     * @param outFullName Output display full name
     * @return true if authentication succeeds
     */
    bool authenticateUser(const QString &username, const QString &password, UserRole &outRole, QString &outFullName);

    // Architecture 2: Distributed Database Atomic Mutex Lock
    /**
     * @brief Attempts to acquire atomic distributed lock on the shared Reference DRX.
     * @param stationId Requesting station identifier
     * @param outLockedByStation Output station ID currently holding lock if denied
     * @param outCurrentChannel Output transponder channel currently being tested
     * @return true if acquired; false if busy
     */
    bool tryAcquireDbLock(int stationId, int &outLockedByStation, int &outCurrentChannel);

    /**
     * @brief Releases atomic distributed lock.
     * @param stationId Station holding the lock
     * @return true on successful release
     */
    bool releaseDbLock(int stationId);

    /**
     * @brief Updates lock heartbeat and currently tested channel to prevent deadlock timeout.
     * @param stationId Holding station identifier
     * @param currentChannel Active transponder channel number
     * @return true on success
     */
    bool updateDbLockHeartbeat(int stationId, int currentChannel);

    /**
     * @brief Returns ID of current station holding reference lock (0 if idle).
     * @param outCurrentChannel Output active channel number
     */
    int getDbLockHolder(int &outCurrentChannel);

signals:
    void connectionStatusChanged(bool connected, const QString &message);

private:
    DbManager(QObject *parent = nullptr);
    ~DbManager() override;
    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    bool initTablesIfSqlite();

    QString m_connectionName = "NetaMainDb";
    QString m_driverName = "QMYSQL";
    QString m_lastError;
    QString m_appDirPath;
    QProcess *m_portableProcess = nullptr;
    bool m_launchedPortableMysqld = false;
};

#endif // DB_MANAGER_H
