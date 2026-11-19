/**
 * @file db_manager.h
 * @brief Thread-safe database management interface for MariaDB/MySQL storage and atomic distributed locking.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <QObject>
#include <QMap>
#include "data_models.h"
#include "db_stream.h"

/**
 * @struct AuditLogRecord
 * @brief Represents an audit trail log entry for operations and tests.
 */
struct AuditLogRecord {
    qint64 id = 0;
    QDateTime timestamp;
    QString actionType;
    QString details;
    QString operatorUsername;
    int stationId = 0;
};

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

    /** @brief Binds an external DbStream abstraction (e.g. custom driver or mock DB). */
    void setStream(DbStream *stream);

    /** @brief Returns the active DbStream abstraction used by DbManager. */
    DbStream* stream() const { return m_stream; }

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
    QString lastError() const;

    /** @brief Returns active SQL driver name (e.g. QMYSQL, QSQLITE). */
    QString driverName() const;

    /** @brief Alias for driverName(), returns active SQL driver name. */
    QString activeDriver() const { return driverName(); }

    /** @brief Returns the Qt SQL connection name used by default physical drivers. */
    QString connectionName() const { return m_connectionName; }

    /** @brief Returns true if active driver is MariaDB/MySQL. */
    bool isUsingMysql() const { return driverName() == "QMYSQL" || driverName() == "QODBC"; }

    /** @brief Returns true if active driver is SQLite. */
    bool isUsingSqlite() const { return driverName() == "QSQLITE"; }

    /** @brief Returns active database name or file path. */
    QString databaseName() const;

    // Automatic Schema & Default Seed Data Provisioning
    bool ensureDefaultDataIfMissing();
    bool executeSqlScript(const QString &resourceOrFilePath);

    // Profile & Golden Sample Management
    QVector<SatelliteProfileInfo> getProfiles();
    SatelliteProfileInfo getProfile(int profileId);
    bool saveProfile(const SatelliteProfileInfo &profile);
    /** @brief Deactivates or deletes a satellite profile by ID (protects profile 1). */
    bool deleteProfile(int profileId);
    /** @brief Stores measured Golden Sample C/N and RF power baseline for a specific profile channel. */
    bool updateChannelGoldenSample(int profileId, int channelNo, double cnDb, double powDbm);

    // Host Standalone Frequency Golden Sample Pool (independent of station satellite profiles)
    /** @brief Returns all frequency-based Golden Sample measurements stored by the Host PC. */
    QVector<GoldenFrequencyRecord> getGoldenFrequencies();
    /** @brief Saves or updates a frequency-based Golden Sample measurement (upserts by freq+sr+pol). */
    bool saveGoldenFrequency(const GoldenFrequencyRecord &rec);
    /** @brief Deletes a frequency-based Golden Sample entry by ID. */
    bool deleteGoldenFrequency(int id);
    /**
     * @brief Toggles the active/inactive state of a Golden Sample entry within its own frequency group.
     * @param id Primary key of the Golden Sample record.
     * @param active True to activate (deactivates other records of the same frequency/SR/pol); false to deactivate.
     * @return true if database update succeeded.
     */
    bool setGoldenFrequencyActive(int id, bool active);
    /** @brief Looks up an active Host Golden Sample measurement matching a transponder frequency/sr/pol. */
    bool findGoldenSampleForChannel(int freqMhz, int symbolRate, const QString &pol,
                                    double &outCn, double &outPow, double &outTol);

    // Work Order Management
    QVector<WorkOrderInfo> getActiveWorkOrders();
    QVector<WorkOrderInfo> getAllWorkOrders();
    bool saveWorkOrder(const WorkOrderInfo &order);
    bool deleteWorkOrder(int orderId);
    bool setWorkOrderStatus(int orderId, const QString &status);
    bool incrementOrderCount(int workOrderId, bool isPass);

    // Audit Logging
    /**
     * @brief Writes an action or event into the audit_logs table and system logs.
     * @param action Action or event type (e.g. TEST_FINISHED, ORDER_CREATED, LOGIN)
     * @param details Text description or metadata
     * @param user Username associated with the action
     * @param stationId Originating station ID (0 for Host)
     * @return true if recorded successfully
     */
    bool logAudit(const QString &action, const QString &details,
                  const QString &user = QString(), int stationId = 0);

    /**
     * @brief Queries historical audit trail records.
     * @param limit Maximum rows to return
     * @param search Filter substring for action, details, or username
     */
    QVector<AuditLogRecord> getAuditLogs(int limit = 200, const QString &search = QString());

    /** @brief Clears historical audit trail records. */
    bool clearAuditLogs();

    // Test Run History & Reporting
    /** @brief Inserts a completed test run or host operation record into test_runs. */
    qint64 insertTestRun(const TestRunData &run);
    /** @brief Queries recent test runs joined with operator username and satellite profile name. */
    QVector<TestRunData> getTestRuns(int limit = 100, const QString &searchSn = QString());
    /** @brief Returns total count of test runs matching filter criteria for high-performance pagination. */
    int getTestRunsCount(const QString &searchNeedle = QString(), int stationFilter = -1, const QString &resultFilter = QString());
    /** @brief Queries a paginated slice of test runs with indexed server-side pagination. */
    QVector<TestRunData> getTestRunsPaged(int page, int pageSize = 50, const QString &searchNeedle = QString(), int stationFilter = -1, const QString &resultFilter = QString());
    /** @brief Checks whether a test run with the same serial number, station, and timestamp already exists. */
    bool testRunExists(const QString &antennaSn, int stationId, const QDateTime &timestamp);

    /** @brief Queries all channel measurement results for a specific test run. */
    QVector<ChannelSampleResult> getTestRunChannels(qint64 testRunId);
    /**
     * @brief Permanently deletes a test run and its associated channel records from the database.
     * @param testRunId Primary key of the test run to remove.
     * @return true if deleted successfully.
     */
    bool deleteTestRun(qint64 testRunId);

    /**
     * @brief Queries an array of arithmetic mean Delta dB attenuation values for Statistical Process Control (SPC).
     * @param workOrderId Filter by work order ID (-1 for all).
     * @param stationId Filter by station ID (-1 for all).
     * @param profileId Filter by satellite profile ID (-1 for all).
     * @param limit Maximum number of recent test runs to analyze.
     * @return Array of double precision delta attenuation values.
     */
    QVector<double> getDeltaDbValuesForSpc(int workOrderId = -1, int stationId = -1, int profileId = -1, int limit = 500);

    // System Settings Key-Value Store
    QString getSetting(const QString &key, const QString &defaultValue = QString());
    bool setSetting(const QString &key, const QString &value, const QString &desc = QString());

    /** @brief Reads the active reference mode (LiveReference vs GoldenSample) set by the Administrator. */
    ReferenceMode getReferenceMode();
    /** @brief Persists the active reference mode (LiveReference vs GoldenSample) in system_settings. */
    bool setReferenceMode(ReferenceMode mode);

    /** @brief Reads the active Golden Sample record ID designated by the Administrator. */
    int getActiveGoldenRecordId();
    /** @brief Persists the active Golden Sample record ID designated by the Administrator in system_settings. */
    bool setActiveGoldenRecordId(int recordId);

    /**
     * @brief Registers or updates a live Host or Station node in the cluster_nodes table.
     * @param nodeType "HOST_SERVER" or "STATION_CLIENT"
     * @param nodeName Display name of the node
     * @param ipAddress IPv4 address of the node
     * @param port Listening TCP port or station numeric ID
     * @param status "ONLINE", "BUSY", or "OFFLINE"
     * @param activeTestSn Active antenna serial number being tested (if any)
     * @param details Simple description of the node
     * @return true on success
     */
    bool upsertClusterNode(const QString &nodeType, const QString &nodeName,
                           const QString &ipAddress, int port,
                           const QString &status,
                           const QString &activeTestSn = QString(),
                           const QString &details = QString());

    /**
     * @brief Removes a node from cluster_nodes when the application exits or disconnects.
     * @param nodeType "HOST_SERVER" or "STATION_CLIENT"
     * @param ipAddress IPv4 address of the node
     * @param port Listening TCP port or station numeric ID
     * @return true on success
     */
    bool removeClusterNode(const QString &nodeType, const QString &ipAddress, int port);

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

    /** @brief Returns all user accounts from the users table for authorization management. */
    QVector<UserRecord> getUsers();
    /** @brief Creates a new user or updates an existing user's role, full name, active flag, and optional password. */
    bool saveUser(const UserRecord &user);
    /** @brief Deletes a user account by ID (protects the built-in admin account). */
    bool deleteUser(int userId);

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
    void ensureDefaultStream();

    QString m_connectionName = "NetaMainDb";
    QString m_lastError;
    DbStream *m_stream = nullptr;
    DbStream *m_ownedStream = nullptr;
    QObject  *m_ownedDriver = nullptr;
};

#endif // DB_MANAGER_H
