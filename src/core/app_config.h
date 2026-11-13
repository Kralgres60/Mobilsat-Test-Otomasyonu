/**
 * @file app_config.h
 * @brief Persistent configuration manager for network, database, hardware, and operational parameters.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <QString>
#include <QSettings>
#include "data_models.h"

/**
 * @enum AppRole
 * @brief Defines the operating mode of the application binary.
 */
enum class AppRole {
    Client, ///< Workstation / Operator test table (Station 1, 2, or 3)
    Host    ///< Central Management Server and Golden Sample Reference Host
};

/**
 * @class AppConfig
 * @brief Thread-safe singleton managing persistent application configurations via QSettings.
 */
class AppConfig {
public:
    /** @brief Returns global configuration singleton instance. */
    static AppConfig& instance();

    /** @brief Loads configuration settings from disk (config.ini). */
    void load();

    /** @brief Flushes and persists current configuration settings to disk. */
    void save();

    // Operational Role
    AppRole role() const { return m_role; }
    void setRole(AppRole role) { m_role = role; }
    bool isHost() const { return m_role == AppRole::Host; }
    bool isClient() const { return m_role == AppRole::Client; }

    // User Privilege Level & Credentials
    UserRole userRole() const { return m_userRole; }
    void setUserRole(UserRole role) { m_userRole = role; }
    bool isEngineer() const { return m_userRole == UserRole::Engineer; }
    bool isTechnician() const { return m_userRole == UserRole::Technician; }

    QString currentUsername() const { return m_currentUsername; }
    void setCurrentUsername(const QString &username) { m_currentUsername = username; }

    QString currentUserFullName() const { return m_currentUserFullName; }
    void setCurrentUserFullName(const QString &fullName) { m_currentUserFullName = fullName; }

    // Station Identity (Client Mode - dynamic ID supported)
    int stationId() const { return m_stationId; }
    void setStationId(int id) { m_stationId = id; }
    QString stationAlias() const { return m_stationAlias; }
    void setStationAlias(const QString &alias) { m_stationAlias = alias; }
    QString serverName() const { return m_serverName; }
    void setServerName(const QString &name) { m_serverName = name; }
    QString stationName() const {
        return m_stationAlias.isEmpty() ? QString("İstasyon %1").arg(m_stationId) : m_stationAlias;
    }

    // Network Topology Parameters
    QString hostIp() const { return m_hostIp; }
    void setHostIp(const QString &ip) { m_hostIp = ip; }
    quint16 tcpPort() const { return m_tcpPort; }
    void setTcpPort(quint16 port) { m_tcpPort = port; }
    quint16 udpBeaconPort() const { return m_udpBeaconPort; }
    void setUdpBeaconPort(quint16 port) { m_udpBeaconPort = port; }

    // MariaDB / MySQL Configuration
    QString dbHost() const { return m_dbHost; }
    void setDbHost(const QString &host) { m_dbHost = host; }
    int dbPort() const { return m_dbPort; }
    void setDbPort(int port) { m_dbPort = port; }
    QString dbName() const { return m_dbName; }
    void setDbName(const QString &name) { m_dbName = name; }
    QString dbUser() const { return m_dbUser; }
    void setDbUser(const QString &user) { m_dbUser = user; }
    QString dbPassword() const { return m_dbPassword; }
    void setDbPassword(const QString &pass) { m_dbPassword = pass; }

    // Serial Communication Parameters (RS232 Fallback)
    QString serialPort() const { return m_serialPort; }
    void setSerialPort(const QString &port) { m_serialPort = port; }
    int serialBaudRate() const { return m_serialBaudRate; }
    void setSerialBaudRate(int baud) { m_serialBaudRate = baud; }

    // RF Test Timing and Tolerance
    int lockTimeoutSec() const { return m_lockTimeoutSec; }
    void setLockTimeoutSec(int sec) { m_lockTimeoutSec = sec; }
    int sampleDurationSec() const { return m_sampleDurationSec; }
    void setSampleDurationSec(int sec) { m_sampleDurationSec = sec; }
    double defaultToleranceDb() const { return m_defaultToleranceDb; }
    void setDefaultToleranceDb(double db) { m_defaultToleranceDb = db; }

    // Report Export Directory
    QString reportDir() const { return m_reportDir; }
    void setReportDir(const QString &dir) { m_reportDir = dir; }

    // Last Selected UI State (Auto-persisted immediately)
    int lastWorkOrderId() const { return m_lastWorkOrderId; }
    void setLastWorkOrderId(int id) { m_lastWorkOrderId = id; save(); }
    int lastProfileId() const { return m_lastProfileId; }
    void setLastProfileId(int id) { m_lastProfileId = id; save(); }
    ReferenceMode lastReferenceMode() const { return m_lastReferenceMode; }
    void setLastReferenceMode(ReferenceMode mode) { m_lastReferenceMode = mode; save(); }

    /** @brief Returns the portable INI configuration file path beside the executable. */
    static QString configFilePath();

    // DRX Web Server / CGI Parameters
    QString refDrxHost() const { return m_refDrxHost; }
    void setRefDrxHost(const QString &host);
    int refDrxPort() const { return m_refDrxPort; }
    void setRefDrxPort(int port) { m_refDrxPort = (port > 0) ? port : 80; }
    QString refDrxEndpoint() const;

    QString stationDrxHost() const { return m_stationDrxHost; }
    void setStationDrxHost(const QString &host);
    int stationDrxPort() const { return m_stationDrxPort; }
    void setStationDrxPort(int port) { m_stationDrxPort = (port > 0) ? port : 80; }
    QString stationDrxEndpoint() const;

    QString drxUsername() const { return m_drxUsername; }
    void setDrxUsername(const QString &u) { m_drxUsername = u; }
    QString drxPassword() const { return m_drxPassword; }
    void setDrxPassword(const QString &p) { m_drxPassword = p; }

    /** @brief Locates and launches NETA_DRX_Simulator.exe companion application. */
    static bool launchSimulator(class QWidget *parent = nullptr);

private:
    AppConfig();
    ~AppConfig() = default;
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    AppRole m_role = AppRole::Client;
    UserRole m_userRole = UserRole::Technician;
    QString m_currentUsername = "operator1";
    QString m_currentUserFullName = "Masa 1 Operatörü";
    int m_stationId = 0; // 0 = Server assigns dynamically on JOIN
    QString m_stationAlias;
    QString m_serverName = "NETA-FABRIKA-HOST";

    QString m_hostIp = "127.0.0.1";
    quint16 m_tcpPort = 9100;
    quint16 m_udpBeaconPort = 9101;

    QString m_dbHost = "127.0.0.1";
    int m_dbPort = 3306;
    QString m_dbName = "neta_antenna_test";
    QString m_dbUser = "root";
    QString m_dbPassword = "";

    QString m_serialPort = "COM1";
    int m_serialBaudRate = 115200;

    // DRX Web Server Defaults
    QString m_refDrxHost = "192.168.0.167";
    int m_refDrxPort = 80;
    QString m_stationDrxHost = "192.168.0.167";
    int m_stationDrxPort = 80;
    QString m_drxUsername = "admin";
    QString m_drxPassword = "1234";

    int m_lockTimeoutSec = 90;
    int m_sampleDurationSec = 5;
    double m_defaultToleranceDb = 1.50;
    QString m_reportDir = "reports";

    int m_lastWorkOrderId = 0;
    int m_lastProfileId = 1;
    ReferenceMode m_lastReferenceMode = ReferenceMode::LiveReference;
    bool m_isLoading = false;
};

#endif // APP_CONFIG_H
