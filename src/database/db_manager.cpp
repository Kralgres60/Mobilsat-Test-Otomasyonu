/**
 * @file db_manager.cpp
 * @brief Implementation of MariaDB/MySQL database management and distributed locking.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "db_manager.h"
#include "mariadb_driver.h"
#include "sqlite_driver.h"
#include "core/app_config.h"
#include <QDebug>
#include <QVariant>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStringList>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QDataStream>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QStandardPaths>

DbManager& DbManager::instance() {
    static DbManager s_instance;
    return s_instance;
}

DbManager::DbManager(QObject *parent) : QObject(parent) {
    m_ownedStream = new DbStream(this);
    m_stream = m_ownedStream;
}

DbManager::~DbManager() {
    closeDatabase();
    stopPortableMariaDbIfRunning();
}

void DbManager::setStream(DbStream *stream) {
    if (stream) {
        m_stream = stream;
    } else {
        m_stream = m_ownedStream;
    }
}

void DbManager::ensureDefaultStream() {
    if (!m_stream) {
        m_stream = m_ownedStream;
    }
    if (m_stream != m_ownedStream || m_stream->isBound()) {
        return;
    }

    if (MariaDbDriver::isAvailable()) {
        auto *drv = new MariaDbDriver(m_connectionName, this);
        m_ownedDriver = drv;
        m_ownedStream->bind(drv->ops());
    } else {
        auto *drv = new SqliteDriver(m_connectionName, this);
        m_ownedDriver = drv;
        m_ownedStream->bind(drv->ops());
    }
}

bool DbManager::connectToDatabase(const QString &host, int port, const QString &dbName,
                                  const QString &user, const QString &password) {
    ensureDefaultStream();

    if (!m_stream->connectToDatabase(host, port, dbName, user, password)) {
        m_lastError = m_stream->lastError();
        emit connectionStatusChanged(false, "Merkezi MariaDB bekleniyor: " + m_lastError);
        return false;
    }

    m_lastError.clear();
    if (isUsingSqlite()) {
        initTablesIfSqlite();
    }
    ensureDefaultDataIfMissing();
    // Synchronize local encrypted user vault immediately upon connecting to MariaDB
    getUsers();
    emit connectionStatusChanged(true, "Connected (" + driverName() + ")");
    return true;
}

void DbManager::closeDatabase() {
    if (m_stream) {
        m_stream->closeDatabase();
    }
}

bool DbManager::isConnected() const {
    return m_stream ? m_stream->isConnected() : false;
}

QString DbManager::driverName() const {
    return m_stream ? m_stream->driverName() : QString();
}

QString DbManager::lastError() const {
    if (m_stream) {
        const QString err = m_stream->lastError();
        if (!err.isEmpty()) return err;
    }
    return m_lastError;
}

QString DbManager::databaseName() const {
    return m_stream ? m_stream->databaseName() : QString();
}

bool DbManager::initTablesIfSqlite() {
    if (!m_stream || !m_stream->isConnected()) return false;
    // Base tables for SQLite
    m_stream->exec("CREATE TABLE IF NOT EXISTS system_settings (setting_key TEXT PRIMARY KEY, setting_value TEXT, description TEXT);");
    m_stream->exec("CREATE TABLE IF NOT EXISTS satellite_profiles (id INTEGER PRIMARY KEY AUTOINCREMENT, profile_name TEXT, satellite_name TEXT, orbital_position TEXT, default_tolerance_db REAL, is_active INTEGER);");
    m_stream->exec("CREATE TABLE IF NOT EXISTS profile_channels (id INTEGER PRIMARY KEY AUTOINCREMENT, profile_id INTEGER, channel_no INTEGER, channel_name TEXT, frequency_mhz INTEGER, symbol_rate INTEGER, polarization TEXT, tolerance_db REAL, golden_cn_db REAL DEFAULT 0.0, golden_pow_dbm REAL DEFAULT 0.0, has_golden_sample INTEGER DEFAULT 0);");
    m_stream->exec("CREATE TABLE IF NOT EXISTS work_orders (id INTEGER PRIMARY KEY AUTOINCREMENT, order_no TEXT, customer_name TEXT, product_code TEXT, satellite_profile_id INTEGER, target_quantity INTEGER, completed_quantity INTEGER, passed_quantity INTEGER, failed_quantity INTEGER, status TEXT, notes TEXT);");
    m_stream->exec("CREATE TABLE IF NOT EXISTS test_runs (id INTEGER PRIMARY KEY AUTOINCREMENT, work_order_id INTEGER, station_id INTEGER, operator_id INTEGER, tester_name TEXT DEFAULT '', operator_username TEXT DEFAULT '', antenna_sn TEXT, satellite_profile_id INTEGER, test_timestamp TEXT, duration_sec INTEGER, overall_result TEXT, fail_reason TEXT, avg_delta_db REAL, max_delta_db REAL, notes TEXT);");
    m_stream->exec("ALTER TABLE test_runs ADD COLUMN operator_username TEXT DEFAULT '';");
    m_stream->exec("ALTER TABLE test_runs ADD COLUMN last_drx_device_time TEXT;");
    m_stream->exec("ALTER TABLE test_runs ADD COLUMN test_end_time TEXT;");
    m_stream->exec("ALTER TABLE test_runs ADD COLUMN dt_reserved_ms INTEGER DEFAULT 0;");
    m_stream->exec("CREATE TABLE IF NOT EXISTS test_run_channels (id INTEGER PRIMARY KEY AUTOINCREMENT, test_run_id INTEGER, channel_no INTEGER, channel_name TEXT, frequency_mhz INTEGER, symbol_rate INTEGER, polarization TEXT, dut_cn_avg REAL, dut_pow_avg REAL, ref_cn_avg REAL, ref_pow_avg REAL, delta_db REAL, carrier_locked INTEGER, video_locked INTEGER, audio_locked INTEGER, tolerance_db REAL, result TEXT);");
    m_stream->exec("ALTER TABLE test_run_channels ADD COLUMN drx_device_time TEXT;");
    m_stream->exec("ALTER TABLE test_run_channels ADD COLUMN sample_host_time TEXT;");
    m_stream->exec("ALTER TABLE test_run_channels ADD COLUMN dt_reserved_ms INTEGER DEFAULT 0;");
    m_stream->exec("CREATE TABLE IF NOT EXISTS golden_frequencies (id INTEGER PRIMARY KEY AUTOINCREMENT, label TEXT DEFAULT '', frequency_mhz INTEGER NOT NULL DEFAULT 0, symbol_rate INTEGER NOT NULL DEFAULT 0, polarization TEXT NOT NULL DEFAULT 'V', golden_cn_db REAL DEFAULT 0.0, golden_pow_dbm REAL DEFAULT 0.0, tolerance_db REAL DEFAULT 1.50, duration_sec INTEGER DEFAULT 0, sample_data TEXT DEFAULT '', is_active INTEGER DEFAULT 1, updated_at TEXT DEFAULT CURRENT_TIMESTAMP);");
    m_stream->exec("ALTER TABLE golden_frequencies ADD COLUMN duration_sec INTEGER DEFAULT 0;");
    m_stream->exec("ALTER TABLE golden_frequencies ADD COLUMN sample_data TEXT DEFAULT '';");
    m_stream->exec("ALTER TABLE golden_frequencies ADD COLUMN is_active INTEGER DEFAULT 1;");
    m_stream->exec("CREATE TABLE IF NOT EXISTS audit_logs (id INTEGER PRIMARY KEY AUTOINCREMENT, log_timestamp TEXT DEFAULT CURRENT_TIMESTAMP, action_type TEXT NOT NULL, details TEXT, operator_username TEXT DEFAULT '', station_id INTEGER DEFAULT 0);");
    m_stream->exec("CREATE TABLE IF NOT EXISTS cluster_nodes (id INTEGER PRIMARY KEY AUTOINCREMENT, node_type TEXT NOT NULL DEFAULT 'HOST_SERVER', node_name TEXT NOT NULL, ip_address TEXT NOT NULL, port INTEGER NOT NULL DEFAULT 45454, status TEXT NOT NULL DEFAULT 'ONLINE', active_test_sn TEXT DEFAULT '', details TEXT DEFAULT '', last_seen TEXT DEFAULT CURRENT_TIMESTAMP, UNIQUE(node_type, ip_address, port));");

    // Performance B-Tree Indexes for SQLite local fallback
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_test_runs_sn ON test_runs (antenna_sn);");
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_test_runs_time ON test_runs (test_timestamp);");
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_test_runs_sn_time ON test_runs (antenna_sn, test_timestamp);");
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_test_runs_wo ON test_runs (work_order_id);");
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_test_runs_station ON test_runs (station_id);");
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_test_runs_result ON test_runs (overall_result);");
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_run_channels_run ON test_run_channels (test_run_id);");
    m_stream->exec("CREATE INDEX IF NOT EXISTS idx_audit_logs_action ON audit_logs (action_type);");

    // Check if default profile exists
    const DbQueryResult res = m_stream->exec("SELECT COUNT(*) FROM satellite_profiles;");
    if (res.ok && res.hasRows() && res.first().at(0).toInt() == 0) {
        m_stream->exec("INSERT INTO satellite_profiles (id, profile_name, satellite_name, orbital_position, default_tolerance_db, is_active) VALUES (1, 'Türksat Standart (10 Kanal)', 'Türksat 42.0E', '42.0E', 1.50, 1);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 1, 'TRT 1 HD', 11054, 30000, 'V', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 2, 'TRT SPOR HD', 11958, 27500, 'V', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 3, 'ATV HD', 12053, 27500, 'H', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 4, 'KANAL D HD', 12245, 27500, 'H', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 5, 'STAR TV HD', 12015, 27500, 'H', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 6, 'SHOW TV HD', 12210, 10000, 'H', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 7, 'A HABER HD', 12053, 27500, 'H', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 8, 'CNN TURK HD', 12245, 27500, 'H', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 9, 'HABERTURK HD', 12209, 10000, 'H', 1.50);");
        m_stream->exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 10, 'NTV HD', 12015, 27500, 'H', 1.50);");

        m_stream->exec("INSERT INTO work_orders (id, order_no, customer_name, product_code, satellite_profile_id, target_quantity, status) VALUES (1, 'NETA-2026-001', 'Genel Üretim', 'DRX9000-ANT-V1', 1, 100, 'IN_PROGRESS');");
    }
    return true;
}

bool DbManager::executeSqlScript(const QString &resourceOrFilePath) {
    QFile file(resourceOrFilePath);
    if (!file.exists()) {
        QString baseName = resourceOrFilePath.mid(resourceOrFilePath.lastIndexOf('/') + 1);
        QStringList candidates = {
            "sql/" + baseName,
            "../sql/" + baseName,
            "../../sql/" + baseName,
            QDir::current().filePath("sql/" + baseName),
            "D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/sql/" + baseName
        };
        for (const QString &cand : candidates) {
            if (QFile::exists(cand)) {
                file.setFileName(cand);
                break;
            }
        }
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[DbManager] Failed to open SQL script file:" << resourceOrFilePath;
        return false;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");
    QString content = in.readAll();
    file.close();

    if (!m_stream || !m_stream->isConnected()) return false;

    QString currentQuery;
    const QStringList lines = content.split('\n');
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith("--") || line.startsWith("/*")) {
            continue;
        }

        // Strip inline comments (-- comment)
        int commentPos = line.indexOf("--");
        if (commentPos >= 0) {
            line = line.left(commentPos).trimmed();
        }
        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith("USE ", Qt::CaseInsensitive)) {
            continue;
        }

        currentQuery += line + "\n";
        if (line.endsWith(";")) {
            currentQuery = currentQuery.trimmed();
            if (currentQuery.endsWith(';')) {
                currentQuery.chop(1);
            }
            currentQuery = currentQuery.trimmed();
            if (!currentQuery.isEmpty()) {
                const DbQueryResult res = m_stream->exec(currentQuery);
                if (!res.ok) {
                    qWarning() << "[DbManager] SQL execution warning:" << res.error
                               << "\nQuery:" << currentQuery.left(120);
                }
            }
            currentQuery.clear();
        }
    }

    if (!currentQuery.trimmed().isEmpty()) {
        QString trailing = currentQuery.trimmed();
        if (trailing.endsWith(';')) {
            trailing.chop(1);
        }
        trailing = trailing.trimmed();
        if (!trailing.isEmpty()) {
            m_stream->exec(trailing);
        }
    }

    return true;
}

bool DbManager::ensureDefaultDataIfMissing() {
    if (!isConnected()) {
        qWarning() << "[DbManager] ensureDefaultDataIfMissing called but database is not connected.";
        return false;
    }

    if (isUsingSqlite()) {
        return initTablesIfSqlite();
    }

    // 1. Check if tables exist (satellite_profiles, profile_channels, golden_frequencies, test_runs, test_run_channels, work_orders)
    bool needSchema = false;
    for (const char *tbl : {"satellite_profiles", "profile_channels", "golden_frequencies", "test_runs", "test_run_channels", "work_orders"}) {
        const DbQueryResult chk = m_stream->exec(QString("SHOW TABLES LIKE '%1';").arg(tbl));
        if (!chk.hasRows() || chk.first().at(0).toString().isEmpty()) {
            needSchema = true;
            break;
        }
    }

    if (needSchema) {
        qInfo() << "[DbManager] Missing MySQL tables detected, loading schema automatically...";
        executeSqlScript(":/sql/schema_mysql.sql");
    }

    // Safety net: directly guarantee test_runs and test_run_channels exist
    m_stream->exec("CREATE TABLE IF NOT EXISTS `test_runs` ("
           "`id` BIGINT AUTO_INCREMENT PRIMARY KEY,"
           "`work_order_id` INT DEFAULT NULL,"
           "`station_id` INT NOT NULL,"
           "`operator_id` INT DEFAULT NULL,"
           "`tester_name` VARCHAR(100) NOT NULL DEFAULT '',"
           "`operator_username` VARCHAR(64) NOT NULL DEFAULT '',"
           "`antenna_sn` VARCHAR(64) NOT NULL,"
           "`satellite_profile_id` INT NOT NULL,"
           "`test_timestamp` DATETIME DEFAULT CURRENT_TIMESTAMP,"
           "`duration_sec` INT NOT NULL DEFAULT 0,"
           "`overall_result` ENUM('PASS', 'FAIL', 'ABORTED') NOT NULL,"
           "`fail_reason` TEXT DEFAULT NULL,"
           "`avg_delta_db` DECIMAL(5, 2) DEFAULT 0.00,"
           "`max_delta_db` DECIMAL(5, 2) DEFAULT 0.00,"
           "`notes` TEXT DEFAULT NULL,"
           "INDEX `idx_antenna_sn` (`antenna_sn`),"
           "INDEX `idx_timestamp` (`test_timestamp`),"
           "INDEX `idx_station` (`station_id`)"
           ") ENGINE=InnoDB;");

    // Guarantee station_id supports large numbers (e.g., up to 1000+ stations)
    m_stream->exec("ALTER TABLE test_runs MODIFY COLUMN station_id INT NOT NULL;");

    m_stream->exec("CREATE TABLE IF NOT EXISTS `test_run_channels` ("
           "`id` BIGINT AUTO_INCREMENT PRIMARY KEY,"
           "`test_run_id` BIGINT NOT NULL,"
           "`channel_no` INT NOT NULL,"
           "`channel_name` VARCHAR(64) NOT NULL,"
           "`frequency_mhz` INT NOT NULL,"
           "`symbol_rate` INT NOT NULL,"
           "`polarization` ENUM('V', 'H') NOT NULL,"
           "`dut_cn_avg` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,"
           "`dut_pow_avg` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,"
           "`ref_cn_avg` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,"
           "`ref_pow_avg` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,"
           "`delta_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,"
           "`carrier_locked` TINYINT(1) NOT NULL DEFAULT 0,"
           "`video_locked` TINYINT(1) NOT NULL DEFAULT 0,"
           "`audio_locked` TINYINT(1) NOT NULL DEFAULT 0,"
           "`tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,"
           "`result` ENUM('PASS', 'FAIL', 'WARNING') NOT NULL,"
           "INDEX `idx_run_channel` (`test_run_id`, `channel_no`)"
           ") ENGINE=InnoDB;");

    // Safety net: create tables directly if missing
    if (!m_stream->exec("SHOW TABLES LIKE 'profile_channels';").hasRows()) {
        m_stream->exec("CREATE TABLE IF NOT EXISTS `profile_channels` ("
               "`id` INT AUTO_INCREMENT PRIMARY KEY,"
               "`profile_id` INT NOT NULL,"
               "`channel_no` INT NOT NULL,"
               "`channel_name` VARCHAR(64) NOT NULL,"
               "`frequency_mhz` INT NOT NULL,"
               "`symbol_rate` INT NOT NULL,"
               "`polarization` ENUM('V', 'H') NOT NULL,"
               "`tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,"
               "`golden_cn_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,"
               "`golden_pow_dbm` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,"
               "`has_golden_sample` TINYINT(1) NOT NULL DEFAULT 0,"
               "`golden_updated_at` TIMESTAMP NULL DEFAULT NULL,"
               "`created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
               "UNIQUE KEY `uk_profile_channel` (`profile_id`, `channel_no`)"
               ") ENGINE=InnoDB;");
    }
    if (!m_stream->exec("SHOW TABLES LIKE 'golden_frequencies';").hasRows()) {
        m_stream->exec("CREATE TABLE IF NOT EXISTS `golden_frequencies` ("
               "`id` INT AUTO_INCREMENT PRIMARY KEY,"
               "`label` VARCHAR(100) NOT NULL DEFAULT '',"
               "`frequency_mhz` INT NOT NULL,"
               "`symbol_rate` INT NOT NULL,"
               "`polarization` ENUM('V', 'H') NOT NULL,"
               "`golden_cn_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,"
               "`golden_pow_dbm` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,"
               "`tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,"
               "`is_active` TINYINT(1) NOT NULL DEFAULT 1,"
               "`updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
               ") ENGINE=InnoDB;");
    }

    // 2. Check default profile and channels
    bool needSeed = false;
    const DbQueryResult profRes = m_stream->exec("SELECT COUNT(*) FROM satellite_profiles;");
    if (!profRes.hasRows() || profRes.first().at(0).toInt() == 0) {
        needSeed = true;
    } else {
        const DbQueryResult chRes = m_stream->exec("SELECT COUNT(*) FROM profile_channels;");
        if (!chRes.hasRows() || chRes.first().at(0).toInt() == 0) {
            needSeed = true;
        }
    }

    if (needSeed) {
        qInfo() << "[DbManager] Default satellite profiles not found, loading seed data...";
        executeSqlScript(":/sql/seed_data.sql");
    }

    // 3. Verify system settings (insert defaults with simple A1-A2 English descriptions)
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('LOCK_TIMEOUT_SEC', '90', 'Safety timeout for reference device lock (seconds)') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('SAMPLE_DURATION_SEC', '5', 'Signal measurement time per channel (seconds)') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('DEFAULT_TOLERANCE_DB', '1.50', 'Default allowed Delta dB limit') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('TCP_PORT', '9100', 'Main Host TCP port for commands and sync') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('UDP_BEACON_PORT', '9101', 'Local network port for auto discovery') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");

    // 4. Check if default work order exists
    const DbQueryResult woRes = m_stream->exec("SELECT COUNT(*) FROM work_orders;");
    if (!woRes.hasRows() || woRes.first().at(0).toInt() == 0) {
        m_stream->exec("INSERT INTO work_orders (id, order_no, customer_name, product_code, satellite_profile_id, target_quantity, status, notes) "
               "VALUES (1, 'NETA-2026-001', 'Genel Üretim', 'DRX9000-ANT-V1', 1, 100, 'IN_PROGRESS', 'Fabrika seri üretim parti 1') "
               "ON DUPLICATE KEY UPDATE order_no=order_no;");
    }

    // 5. Algorithm 2 Distributed Reference Lock Table
    m_stream->exec("CREATE TABLE IF NOT EXISTS reference_lock ("
           "id INT PRIMARY KEY DEFAULT 1, "
           "locked_by_station INT NOT NULL DEFAULT 0, "
           "current_channel_no INT NOT NULL DEFAULT 0, "
           "lock_acquired_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, "
           "heartbeat_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP) ENGINE=InnoDB;");
    m_stream->exec("INSERT INTO reference_lock (id, locked_by_station, current_channel_no) VALUES (1, 0, 0) "
           "ON DUPLICATE KEY UPDATE id=id;");

    // 6. Schema migration: add Golden Sample columns to profile_channels if missing
    if (!m_stream->exec("SHOW COLUMNS FROM profile_channels LIKE 'golden_cn_db';").hasRows()) {
        m_stream->exec("ALTER TABLE profile_channels "
               "ADD COLUMN golden_cn_db DECIMAL(5, 2) NOT NULL DEFAULT 0.00 AFTER tolerance_db, "
               "ADD COLUMN golden_pow_dbm DECIMAL(6, 2) NOT NULL DEFAULT 0.00 AFTER golden_cn_db, "
               "ADD COLUMN has_golden_sample TINYINT(1) NOT NULL DEFAULT 0 AFTER golden_pow_dbm, "
               "ADD COLUMN golden_updated_at TIMESTAMP NULL DEFAULT NULL AFTER has_golden_sample;");
    }

    // 6b. Schema migration: add tester_name / operator_username to test_runs if missing
    if (!m_stream->exec("SHOW COLUMNS FROM test_runs LIKE 'tester_name';").hasRows()) {
        m_stream->exec("ALTER TABLE test_runs ADD COLUMN tester_name VARCHAR(100) NOT NULL DEFAULT '' AFTER operator_id;");
    }
    if (!m_stream->exec("SHOW COLUMNS FROM test_runs LIKE 'operator_username';").hasRows()) {
        m_stream->exec("ALTER TABLE test_runs ADD COLUMN operator_username VARCHAR(64) NOT NULL DEFAULT '' AFTER tester_name;");
    }

    // 6c. Host direct measurement & frequency-based Golden Sample pool table
    m_stream->exec("CREATE TABLE IF NOT EXISTS golden_frequencies ("
           "id INT AUTO_INCREMENT PRIMARY KEY, "
           "label VARCHAR(100) NOT NULL DEFAULT '', "
           "frequency_mhz INT NOT NULL DEFAULT 0, "
           "symbol_rate INT NOT NULL DEFAULT 0, "
           "polarization ENUM('V', 'H') NOT NULL DEFAULT 'V', "
           "golden_cn_db DECIMAL(5, 2) NOT NULL DEFAULT 0.00, "
           "golden_pow_dbm DECIMAL(6, 2) NOT NULL DEFAULT 0.00, "
           "tolerance_db DECIMAL(4, 2) NOT NULL DEFAULT 1.50, "
           "duration_sec INT NOT NULL DEFAULT 0, "
           "sample_data MEDIUMTEXT DEFAULT NULL, "
           "is_active TINYINT(1) NOT NULL DEFAULT 1, "
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP) ENGINE=InnoDB;");

    if (!m_stream->exec("SHOW COLUMNS FROM golden_frequencies LIKE 'duration_sec';").hasRows()) {
        m_stream->exec("ALTER TABLE golden_frequencies ADD COLUMN duration_sec INT NOT NULL DEFAULT 0;");
    }
    if (!m_stream->exec("SHOW COLUMNS FROM golden_frequencies LIKE 'sample_data';").hasRows()) {
        m_stream->exec("ALTER TABLE golden_frequencies ADD COLUMN sample_data MEDIUMTEXT DEFAULT NULL;");
    }
    if (!m_stream->exec("SHOW COLUMNS FROM golden_frequencies LIKE 'is_active';").hasRows()) {
        m_stream->exec("ALTER TABLE golden_frequencies ADD COLUMN is_active TINYINT(1) NOT NULL DEFAULT 1;");
    }
    // Drop any legacy unique keys if present so multiple records can exist per frequency with independent active/inactive toggle
    for (const char *idxName : {"uk_golden_freq", "uk_golden_freq_key"}) {
        if (m_stream->exec(QString("SHOW INDEX FROM golden_frequencies WHERE Key_name = '%1';").arg(idxName)).hasRows()) {
            m_stream->exec(QString("ALTER TABLE golden_frequencies DROP INDEX `%1`;").arg(idxName));
        }
    }

    // 6d. System Event & Audit Logs table
    m_stream->exec("CREATE TABLE IF NOT EXISTS audit_logs ("
           "id BIGINT AUTO_INCREMENT PRIMARY KEY, "
           "log_timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
           "action_type VARCHAR(64) NOT NULL, "
           "details TEXT, "
           "operator_username VARCHAR(64) DEFAULT '', "
           "station_id INT DEFAULT 0, "
           "INDEX idx_audit_time (log_timestamp), "
           "INDEX idx_audit_station (station_id)"
           ") ENGINE=InnoDB;");

    // 6e. Performance B-Tree composite indexes for test_runs (fast search & pagination)
    if (!m_stream->exec("SHOW INDEX FROM test_runs WHERE Key_name = 'idx_test_runs_sn_time';").hasRows()) {
        m_stream->exec("ALTER TABLE test_runs ADD INDEX idx_test_runs_sn_time (antenna_sn, test_timestamp);");
    }
    if (!m_stream->exec("SHOW INDEX FROM test_runs WHERE Key_name = 'idx_test_runs_wo';").hasRows()) {
        m_stream->exec("ALTER TABLE test_runs ADD INDEX idx_test_runs_wo (work_order_id);");
    }
    if (!m_stream->exec("SHOW INDEX FROM test_runs WHERE Key_name = 'idx_test_runs_result';").hasRows()) {
        m_stream->exec("ALTER TABLE test_runs ADD INDEX idx_test_runs_result (overall_result);");
    }

    // 6f. Timestamp and future dt telemetry columns in test_runs and test_run_channels
    if (!m_stream->exec("SHOW COLUMNS FROM test_runs LIKE 'last_drx_device_time';").hasRows()) {
        m_stream->exec("ALTER TABLE test_runs ADD COLUMN last_drx_device_time VARCHAR(64) DEFAULT NULL AFTER notes, "
               "ADD COLUMN test_end_time DATETIME DEFAULT NULL AFTER last_drx_device_time, "
               "ADD COLUMN dt_reserved_ms BIGINT DEFAULT 0 AFTER test_end_time;");
    }

    if (!m_stream->exec("SHOW COLUMNS FROM test_run_channels LIKE 'drx_device_time';").hasRows()) {
        m_stream->exec("ALTER TABLE test_run_channels ADD COLUMN drx_device_time VARCHAR(64) DEFAULT NULL AFTER tolerance_db, "
               "ADD COLUMN sample_host_time DATETIME DEFAULT NULL AFTER drx_device_time, "
               "ADD COLUMN dt_reserved_ms BIGINT DEFAULT 0 AFTER sample_host_time;");
    }

    // Insert default Reference Mode if missing
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('REFERENCE_MODE', 'LIVE', 'Reference mode: LIVE (Live Antenna) or GOLDEN (Saved Golden Sample)') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('ACTIVE_GOLDEN_RECORD_ID', '0', 'Active Golden Sample record ID selected by Admin') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");
    m_stream->exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('REPORT_OUTPUT_DIR', 'reports', 'Folder path to save test reports') "
           "ON DUPLICATE KEY UPDATE description = VALUES(description);");

    // 6g. Ensure cluster_nodes table exists and remove any legacy mock/seed entries
    m_stream->exec("CREATE TABLE IF NOT EXISTS cluster_nodes ("
           "id INT AUTO_INCREMENT PRIMARY KEY, "
           "node_type ENUM('HOST_SERVER', 'STATION_CLIENT', 'WEB_SERVICE') NOT NULL DEFAULT 'HOST_SERVER', "
           "node_name VARCHAR(100) NOT NULL, "
           "ip_address VARCHAR(45) NOT NULL, "
           "port INT NOT NULL DEFAULT 45454, "
           "status ENUM('ONLINE', 'BUSY', 'OFFLINE') NOT NULL DEFAULT 'ONLINE', "
           "active_test_sn VARCHAR(64) DEFAULT NULL, "
           "details VARCHAR(255) DEFAULT NULL, "
           "last_seen TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, "
           "UNIQUE KEY uk_node_ip_port (node_type, ip_address, port)"
           ") ENGINE=InnoDB;");
    m_stream->exec("DELETE FROM cluster_nodes WHERE node_name IN ("
           "'NETA-HOST-ANA-HAT', 'NETA-HOST-ARGE-02', "
           "'ISTASYON-MASA-1', 'ISTASYON-MASA-2', 'ISTASYON-MASA-3', "
           "'Ana Yönetici Sunucu (Host #1)', 'Yedek Kalite Kontrol Sunucusu (Host #2)', "
           "'İstasyon 1 (Test Masası #1)', 'İstasyon 2 (Test Masası #2)', 'İstasyon 3 (Test Masası #3)');");

    // 7. Repair any corrupted seed text from legacy sessions
    m_stream->exec("SET NAMES 'utf8mb4'");
    m_stream->exec("UPDATE satellite_profiles "
           "SET profile_name = 'Türksat Standart Üretim Testi (10 Kanal)', "
           "    satellite_name = 'Türksat 42.0°E' "
           "WHERE id = 1 AND (profile_name LIKE '%Ã%' OR satellite_name LIKE '%Ã%' OR satellite_name LIKE '%Â%');");
    m_stream->exec("UPDATE work_orders "
           "SET customer_name = 'Genel Üretim' "
           "WHERE id = 1 AND customer_name LIKE '%Ã%';");
    m_stream->exec("UPDATE users SET full_name = 'Sistem Yöneticisi' WHERE username = 'admin' AND full_name LIKE '%Ã%';");
    m_stream->exec("UPDATE users SET full_name = 'Masa 1 Operatörü' WHERE username = 'operator1' AND full_name LIKE '%Ã%';");
    m_stream->exec("UPDATE users SET full_name = 'Masa 2 Operatörü' WHERE username = 'operator2' AND full_name LIKE '%Ã%';");
    m_stream->exec("UPDATE users SET full_name = 'Masa 3 Operatörü' WHERE username = 'operator3' AND full_name LIKE '%Ã%';");
    m_stream->exec("UPDATE users SET password_hash = '123' WHERE password_hash IN ('neta123', 'op123', 'admin', '1234', 'tech');");

    qInfo() << "[Database Service] Database schema and factory default values ready.";
    return true;
}

QVector<SatelliteProfileInfo> DbManager::getProfiles() {
    QVector<SatelliteProfileInfo> list;
    if (!isConnected()) return list;

    const DbQueryResult res = m_stream->exec("SELECT id, profile_name, satellite_name, orbital_position, default_tolerance_db, is_active FROM satellite_profiles WHERE is_active = 1 ORDER BY id ASC");

    for (const DbRow &row : res.rows) {
        SatelliteProfileInfo p;
        p.id = row.at(0).toInt();
        p.profileName = row.at(1).toString();
        p.satelliteName = row.at(2).toString();
        p.orbitalPosition = row.at(3).toString();
        p.defaultToleranceDb = row.at(4).toDouble();
        p.isActive = row.at(5).toBool();

        // Populate channels and Golden Sample baseline data
        const DbQueryResult chRes = m_stream->execNamed(
            "SELECT channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db, "
            "golden_cn_db, golden_pow_dbm, has_golden_sample "
            "FROM profile_channels WHERE profile_id = :pid ORDER BY channel_no ASC",
            {{":pid", p.id}});
        if (chRes.ok) {
            for (const DbRow &chRow : chRes.rows) {
                ChannelConfig c;
                c.channelNo = chRow.at(0).toInt();
                c.channelName = chRow.at(1).toString();
                c.frequencyMhz = chRow.at(2).toInt();
                c.symbolRate = chRow.at(3).toInt();
                c.polarization = chRow.at(4).toString();
                c.toleranceDb = chRow.at(5).toDouble();
                c.goldenCnDb = chRow.at(6).toDouble();
                c.goldenPowDbm = chRow.at(7).toDouble();
                c.hasGoldenSample = chRow.at(8).toBool();
                p.channels.append(c);
            }
        }
        list.append(p);
    }
    return list;
}

SatelliteProfileInfo DbManager::getProfile(int profileId) {
    auto all = getProfiles();
    for (const auto &p : all) {
        if (p.id == profileId) return p;
    }
    return SatelliteProfileInfo();
}

bool DbManager::saveProfile(const SatelliteProfileInfo &profile) {
    if (!isConnected()) {
        if (m_lastError.isEmpty()) {
            m_lastError = "Veritabanı bağlantısı aktif değil.";
        }
        return false;
    }
    m_stream->beginTransaction();

    int profileId = profile.id;
    if (profileId <= 0) {
        const DbQueryResult insRes = m_stream->execNamed(
            "INSERT INTO satellite_profiles (profile_name, satellite_name, orbital_position, default_tolerance_db, is_active) "
            "VALUES (:pname, :sname, :orb, :tol, :act)",
            {{":pname", profile.profileName},
             {":sname", profile.satelliteName},
             {":orb", profile.orbitalPosition},
             {":tol", profile.defaultToleranceDb},
             {":act", profile.isActive ? 1 : 0}});
        if (!insRes.ok) {
            m_lastError = insRes.error;
            m_stream->rollbackTransaction();
            return false;
        }
        profileId = static_cast<int>(insRes.lastInsertId);
    } else {
        const DbQueryResult updRes = m_stream->execNamed(
            "UPDATE satellite_profiles SET profile_name = :pname, satellite_name = :sname, "
            "orbital_position = :orb, default_tolerance_db = :tol, is_active = :act WHERE id = :id",
            {{":pname", profile.profileName},
             {":sname", profile.satelliteName},
             {":orb", profile.orbitalPosition},
             {":tol", profile.defaultToleranceDb},
             {":act", profile.isActive ? 1 : 0},
             {":id", profileId}});
        if (!updRes.ok) {
            m_lastError = updRes.error;
            m_stream->rollbackTransaction();
            return false;
        }
    }

    // Clear existing channels and insert updated list
    DbQueryResult delRes = m_stream->execNamed("DELETE FROM profile_channels WHERE profile_id = :pid", {{":pid", profileId}});
    if (!delRes.ok) {
        if (delRes.error.contains("doesn't exist", Qt::CaseInsensitive)) {
            m_stream->rollbackTransaction();
            ensureDefaultDataIfMissing();
            m_stream->beginTransaction();
            delRes = m_stream->execNamed("DELETE FROM profile_channels WHERE profile_id = :pid", {{":pid", profileId}});
            if (!delRes.ok) {
                m_lastError = delRes.error;
                m_stream->rollbackTransaction();
                return false;
            }
        } else {
            m_lastError = delRes.error;
            m_stream->rollbackTransaction();
            return false;
        }
    }

    for (const auto &ch : profile.channels) {
        const DbQueryResult chIns = m_stream->execNamed(
            "INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, "
            "symbol_rate, polarization, tolerance_db, golden_cn_db, golden_pow_dbm, has_golden_sample) "
            "VALUES (:pid, :cno, :cname, :freq, :sr, :pol, :tol, :gcn, :gpow, :hasg)",
            {{":pid", profileId},
             {":cno", ch.channelNo},
             {":cname", ch.channelName},
             {":freq", ch.frequencyMhz},
             {":sr", ch.symbolRate},
             {":pol", ch.polarization},
             {":tol", ch.toleranceDb},
             {":gcn", ch.goldenCnDb},
             {":gpow", ch.goldenPowDbm},
             {":hasg", ch.hasGoldenSample ? 1 : 0}});
        if (!chIns.ok) {
            m_lastError = chIns.error;
            m_stream->rollbackTransaction();
            return false;
        }
    }

    if (!m_stream->commitTransaction()) return false;
    logAudit(profile.id > 0 ? "PROFILE_UPDATED" : "PROFILE_CREATED",
             QString("Profil: %1 (%2 kanal)").arg(profile.profileName).arg(profile.channels.size()),
             AppConfig::instance().currentUsername(),
             AppConfig::instance().role() == AppRole::Client ? AppConfig::instance().stationId() : 0);
    return true;
}

bool DbManager::deleteProfile(int profileId) {
    if (!isConnected() || profileId <= 1) return false; // Varsayılan profil 1 silinmez
    if (!m_stream->execNamed("UPDATE satellite_profiles SET is_active = 0 WHERE id = :id", {{":id", profileId}}).ok) {
        return false;
    }
    logAudit("PROFILE_DELETED", QString("Profil ID: %1 silindi").arg(profileId),
             AppConfig::instance().currentUsername(),
             AppConfig::instance().role() == AppRole::Client ? AppConfig::instance().stationId() : 0);
    return true;
}

bool DbManager::updateChannelGoldenSample(int profileId, int channelNo, double cnDb, double powDbm) {
    if (!isConnected() || profileId <= 0) return false;
    const QString sql = isUsingSqlite()
        ? "UPDATE profile_channels SET golden_cn_db = :cn, golden_pow_dbm = :pow, has_golden_sample = 1 "
          "WHERE profile_id = :pid AND channel_no = :cno"
        : "UPDATE profile_channels SET golden_cn_db = :cn, golden_pow_dbm = :pow, has_golden_sample = 1, "
          "golden_updated_at = CURRENT_TIMESTAMP WHERE profile_id = :pid AND channel_no = :cno";
    return m_stream->execNamed(sql, {{":cn", cnDb}, {":pow", powDbm}, {":pid", profileId}, {":cno", channelNo}}).ok;
}

ReferenceMode DbManager::getReferenceMode() {
    QString val = getSetting("REFERENCE_MODE", "LIVE");
    return (val.compare("GOLDEN", Qt::CaseInsensitive) == 0)
               ? ReferenceMode::GoldenSample
               : ReferenceMode::LiveReference;
}

bool DbManager::setReferenceMode(ReferenceMode mode) {
    QString val = (mode == ReferenceMode::GoldenSample) ? "GOLDEN" : "LIVE";
    bool ok = setSetting("REFERENCE_MODE", val,
                         "Reference mode: LIVE (Live Antenna) or GOLDEN (Saved Golden Sample)");
    if (ok) {
        logAudit("REF_MODE_CHANGED",
                 QString("Referans modu değiştirildi: %1").arg(val),
                 AppConfig::instance().currentUsername(), 0);
    }
    return ok;
}

int DbManager::getActiveGoldenRecordId() {
    return getSetting("ACTIVE_GOLDEN_RECORD_ID", "0").toInt();
}

bool DbManager::setActiveGoldenRecordId(int recordId) {
    return setSetting("ACTIVE_GOLDEN_RECORD_ID", QString::number(recordId),
                      "Active Golden Sample record ID selected by Admin");
}

bool DbManager::upsertClusterNode(const QString &nodeType, const QString &nodeName,
                                  const QString &ipAddress, int port,
                                  const QString &status,
                                  const QString &activeTestSn,
                                  const QString &details) {
    if (!isConnected()) return false;
    const QString sql = isUsingSqlite()
        ? "INSERT OR REPLACE INTO cluster_nodes "
          "(node_type, node_name, ip_address, port, status, active_test_sn, details, last_seen) "
          "VALUES (:type, :name, :ip, :port, :status, :sn, :details, CURRENT_TIMESTAMP)"
        : "INSERT INTO cluster_nodes "
          "(node_type, node_name, ip_address, port, status, active_test_sn, details, last_seen) "
          "VALUES (:type, :name, :ip, :port, :status, :sn, :details, CURRENT_TIMESTAMP) "
          "ON DUPLICATE KEY UPDATE "
          "node_name = VALUES(node_name), "
          "status = VALUES(status), "
          "active_test_sn = VALUES(active_test_sn), "
          "details = VALUES(details), "
          "last_seen = CURRENT_TIMESTAMP";
    return m_stream->execNamed(sql, {
        {":type", nodeType},
        {":name", nodeName},
        {":ip", ipAddress.isEmpty() ? QString("127.0.0.1") : ipAddress},
        {":port", port},
        {":status", status},
        {":sn", activeTestSn},
        {":details", details}
    }).ok;
}

bool DbManager::removeClusterNode(const QString &nodeType, const QString &ipAddress, int port) {
    if (!isConnected()) return false;
    return m_stream->execNamed(
        "DELETE FROM cluster_nodes WHERE node_type = :type AND ip_address = :ip AND port = :port",
        {{":type", nodeType},
         {":ip", ipAddress.isEmpty() ? QString("127.0.0.1") : ipAddress},
         {":port", port}}).ok;
}

QVector<GoldenFrequencyRecord> DbManager::getGoldenFrequencies() {
    QVector<GoldenFrequencyRecord> list;
    if (!isConnected()) return list;

    const DbQueryResult res = m_stream->exec(
        "SELECT id, label, frequency_mhz, symbol_rate, polarization, "
        "golden_cn_db, golden_pow_dbm, tolerance_db, duration_sec, sample_data, is_active, updated_at "
        "FROM golden_frequencies ORDER BY id DESC");
    for (const DbRow &row : res.rows) {
        GoldenFrequencyRecord r;
        r.id = row.at(0).toInt();
        r.label = row.at(1).toString();
        r.frequencyMhz = row.at(2).toInt();
        r.symbolRate = row.at(3).toInt();
        r.polarization = row.at(4).toString();
        r.goldenCnDb = row.at(5).toDouble();
        r.goldenPowDbm = row.at(6).toDouble();
        r.toleranceDb = row.at(7).toDouble();
        r.durationSec = row.at(8).toInt();
        r.sampleData = row.at(9).toString();
        r.isActive = row.at(10).isNull() ? true : row.at(10).toBool();
        r.updatedAt = row.at(11).toString();
        list.append(r);
    }
    return list;
}

bool DbManager::saveGoldenFrequency(const GoldenFrequencyRecord &rec) {
    if (!isConnected()) return false;

    // If inserting a new active record for a frequency, deactivate previous records of the same frequency+pol
    if (rec.id == 0 && rec.isActive && rec.frequencyMhz > 0) {
        m_stream->execNamed(
            "UPDATE golden_frequencies SET is_active = 0 WHERE frequency_mhz = :freq AND polarization = :pol",
            {{":freq", rec.frequencyMhz}, {":pol", rec.polarization}});
    }

    QString sql;
    QVariantMap binds = {
        {":lbl", rec.label},
        {":freq", rec.frequencyMhz},
        {":sr", rec.symbolRate},
        {":pol", rec.polarization},
        {":cn", rec.goldenCnDb},
        {":pow", rec.goldenPowDbm},
        {":tol", rec.toleranceDb},
        {":dur", rec.durationSec},
        {":samples", rec.sampleData},
        {":act", rec.isActive ? 1 : 0}
    };

    if (isUsingSqlite()) {
        if (rec.id > 0) {
            sql = "UPDATE golden_frequencies SET label = :lbl, frequency_mhz = :freq, symbol_rate = :sr, "
                  "polarization = :pol, golden_cn_db = :cn, golden_pow_dbm = :pow, tolerance_db = :tol, "
                  "duration_sec = :dur, sample_data = :samples, is_active = :act, updated_at = CURRENT_TIMESTAMP WHERE id = :id";
            binds.insert(":id", rec.id);
        } else {
            sql = "INSERT INTO golden_frequencies (label, frequency_mhz, symbol_rate, polarization, "
                  "golden_cn_db, golden_pow_dbm, tolerance_db, duration_sec, sample_data, is_active, updated_at) "
                  "VALUES (:lbl, :freq, :sr, :pol, :cn, :pow, :tol, :dur, :samples, :act, CURRENT_TIMESTAMP)";
        }
    } else if (rec.id > 0) {
        sql = "UPDATE golden_frequencies SET label = :lbl, frequency_mhz = :freq, symbol_rate = :sr, "
              "polarization = :pol, golden_cn_db = :cn, golden_pow_dbm = :pow, tolerance_db = :tol, "
              "duration_sec = :dur, sample_data = :samples, is_active = :act WHERE id = :id";
        binds.insert(":id", rec.id);
    } else {
        sql = "INSERT INTO golden_frequencies (label, frequency_mhz, symbol_rate, polarization, "
              "golden_cn_db, golden_pow_dbm, tolerance_db, duration_sec, sample_data, is_active) "
              "VALUES (:lbl, :freq, :sr, :pol, :cn, :pow, :tol, :dur, :samples, :act)";
    }

    const DbQueryResult res = m_stream->execNamed(sql, binds);
    if (!res.ok) {
        m_lastError = res.error;
        return false;
    }
    logAudit(rec.id > 0 ? "GOLDEN_UPDATED" : "GOLDEN_SAVED",
             QString("Golden: %1 (%2 MHz %3, C/N: %4 dB)")
                 .arg(rec.label.isEmpty() ? "--" : rec.label)
                 .arg(rec.frequencyMhz)
                 .arg(rec.polarization)
                 .arg(rec.goldenCnDb, 0, 'f', 2),
             AppConfig::instance().currentUsername(), 0);
    return true;
}

bool DbManager::deleteGoldenFrequency(int id) {
    if (!isConnected() || id <= 0) return false;
    return m_stream->execNamed("DELETE FROM golden_frequencies WHERE id = :id", {{":id", id}}).ok;
}

bool DbManager::setGoldenFrequencyActive(int id, bool active) {
    if (!isConnected() || id <= 0) return false;

    if (active) {
        const DbQueryResult lookup = m_stream->execNamed(
            "SELECT frequency_mhz, polarization FROM golden_frequencies WHERE id = :id",
            {{":id", id}});
        if (lookup.ok && lookup.hasRows()) {
            const int freq = lookup.first().at(0).toInt();
            const QString pol = lookup.first().at(1).toString();
            m_stream->execNamed(
                "UPDATE golden_frequencies SET is_active = 0 "
                "WHERE frequency_mhz = :f AND polarization = :pol AND id != :id",
                {{":f", freq}, {":pol", pol}, {":id", id}});
        }
    }

    return m_stream->execNamed(
        "UPDATE golden_frequencies SET is_active = :act WHERE id = :id",
        {{":act", active ? 1 : 0}, {":id", id}}).ok;
}

bool DbManager::findGoldenSampleForChannel(int freqMhz, int symbolRate, const QString &pol,
                                           double &outCn, double &outPow, double &outTol) {
    if (!isConnected()) return false;

    // 1. Priority: Exact active frequency + symbol rate + polarization match
    const DbQueryResult exactRes = m_stream->execNamed(
        "SELECT golden_cn_db, golden_pow_dbm, tolerance_db FROM golden_frequencies "
        "WHERE frequency_mhz = :f AND symbol_rate = :sr AND polarization = :pol AND is_active = 1 "
        "ORDER BY id DESC LIMIT 1",
        {{":f", freqMhz}, {":sr", symbolRate}, {":pol", pol}});
    if (exactRes.ok && exactRes.hasRows()) {
        outCn = exactRes.first().at(0).toDouble();
        outPow = exactRes.first().at(1).toDouble();
        outTol = exactRes.first().at(2).toDouble();
        return (outCn > 0.1 || qAbs(outPow) > 0.1);
    }

    // 2. Secondary: Active frequency + polarization match
    const DbQueryResult secRes = m_stream->execNamed(
        "SELECT golden_cn_db, golden_pow_dbm, tolerance_db FROM golden_frequencies "
        "WHERE frequency_mhz = :f AND polarization = :pol AND is_active = 1 "
        "ORDER BY id DESC LIMIT 1",
        {{":f", freqMhz}, {":pol", pol}});
    if (secRes.ok && secRes.hasRows()) {
        outCn = secRes.first().at(0).toDouble();
        outPow = secRes.first().at(1).toDouble();
        outTol = secRes.first().at(2).toDouble();
        return (outCn > 0.1 || qAbs(outPow) > 0.1);
    }

    return false;
}

QVector<WorkOrderInfo> DbManager::getActiveWorkOrders() {
    QVector<WorkOrderInfo> list;
    if (!isConnected()) return list;

    const DbQueryResult res = m_stream->exec(
        "SELECT id, order_no, customer_name, product_code, satellite_profile_id, target_quantity, "
        "completed_quantity, passed_quantity, failed_quantity, status, notes "
        "FROM work_orders WHERE status != 'COMPLETED' AND status != 'CANCELLED' ORDER BY id DESC");

    for (const DbRow &row : res.rows) {
        WorkOrderInfo o;
        o.id = row.at(0).toInt();
        o.orderNo = row.at(1).toString();
        o.customerName = row.at(2).toString();
        o.productCode = row.at(3).toString();
        o.satelliteProfileId = row.at(4).toInt();
        o.targetQuantity = row.at(5).toInt();
        o.completedQuantity = row.at(6).toInt();
        o.passedQuantity = row.at(7).toInt();
        o.failedQuantity = row.at(8).toInt();
        o.status = row.at(9).toString();
        o.notes = row.at(10).toString();
        list.append(o);
    }
    return list;
}

QVector<WorkOrderInfo> DbManager::getAllWorkOrders() {
    QVector<WorkOrderInfo> list;
    if (!isConnected()) return list;

    const DbQueryResult res = m_stream->exec(
        "SELECT id, order_no, customer_name, product_code, satellite_profile_id, target_quantity, "
        "completed_quantity, passed_quantity, failed_quantity, status, notes "
        "FROM work_orders ORDER BY id DESC");

    for (const DbRow &row : res.rows) {
        WorkOrderInfo o;
        o.id = row.at(0).toInt();
        o.orderNo = row.at(1).toString();
        o.customerName = row.at(2).toString();
        o.productCode = row.at(3).toString();
        o.satelliteProfileId = row.at(4).toInt();
        o.targetQuantity = row.at(5).toInt();
        o.completedQuantity = row.at(6).toInt();
        o.passedQuantity = row.at(7).toInt();
        o.failedQuantity = row.at(8).toInt();
        o.status = row.at(9).toString();
        o.notes = row.at(10).toString();
        list.append(o);
    }
    return list;
}

bool DbManager::saveWorkOrder(const WorkOrderInfo &order) {
    if (!isConnected()) return false;

    QString sql;
    QVariantMap binds = {
        {":ord", order.orderNo.trimmed()},
        {":cust", order.customerName.trimmed()},
        {":prod", order.productCode.trimmed()},
        {":prof", order.satelliteProfileId > 0 ? order.satelliteProfileId : 1},
        {":tqty", order.targetQuantity},
        {":st", order.status.isEmpty() ? "IN_PROGRESS" : order.status},
        {":notes", order.notes}
    };

    if (order.id > 0) {
        sql = "UPDATE work_orders SET order_no = :ord, customer_name = :cust, product_code = :prod, "
              "satellite_profile_id = :prof, target_quantity = :tqty, status = :st, notes = :notes "
              "WHERE id = :id";
        binds.insert(":id", order.id);
    } else {
        sql = "INSERT INTO work_orders (order_no, customer_name, product_code, satellite_profile_id, "
              "target_quantity, completed_quantity, passed_quantity, failed_quantity, status, notes) "
              "VALUES (:ord, :cust, :prod, :prof, :tqty, :cqty, :pqty, :fqty, :st, :notes)";
        binds.insert(":cqty", order.completedQuantity);
        binds.insert(":pqty", order.passedQuantity);
        binds.insert(":fqty", order.failedQuantity);
    }

    const DbQueryResult res = m_stream->execNamed(sql, binds);
    if (!res.ok) {
        m_lastError = res.error;
        qWarning() << "[DbManager] saveWorkOrder error:" << m_lastError;
    } else {
        logAudit(order.id > 0 ? "ORDER_UPDATED" : "ORDER_CREATED",
                 QString("İş Emri: %1 | Müşteri: %2 | Hedef: %3").arg(order.orderNo, order.customerName).arg(order.targetQuantity),
                 AppConfig::instance().currentUsername(), 0);
    }
    return res.ok;
}

bool DbManager::deleteWorkOrder(int orderId) {
    if (!isConnected() || orderId <= 0) return false;
    const DbQueryResult res = m_stream->execNamed("DELETE FROM work_orders WHERE id = :id", {{":id", orderId}});
    if (!res.ok) {
        m_lastError = res.error;
    } else {
        logAudit("ORDER_DELETED", QString("İş Emri ID: %1 silindi").arg(orderId), AppConfig::instance().currentUsername(), 0);
    }
    return res.ok;
}

bool DbManager::setWorkOrderStatus(int orderId, const QString &status) {
    if (!isConnected() || orderId <= 0) return false;
    const DbQueryResult res = m_stream->execNamed(
        "UPDATE work_orders SET status = :st WHERE id = :id",
        {{":st", status}, {":id", orderId}});
    if (!res.ok) {
        m_lastError = res.error;
    } else {
        logAudit("ORDER_STATUS_CHANGED", QString("İş Emri ID: %1 -> %2").arg(orderId).arg(status), AppConfig::instance().currentUsername(), 0);
    }
    return res.ok;
}

bool DbManager::incrementOrderCount(int workOrderId, bool isPass) {
    if (!isConnected() || workOrderId <= 0) return false;
    const QString sql = isPass
        ? "UPDATE work_orders SET completed_quantity = completed_quantity + 1, passed_quantity = passed_quantity + 1 WHERE id = :id"
        : "UPDATE work_orders SET completed_quantity = completed_quantity + 1, failed_quantity = failed_quantity + 1 WHERE id = :id";
    return m_stream->execNamed(sql, {{":id", workOrderId}}).ok;
}

bool DbManager::logAudit(const QString &action, const QString &details, const QString &user, int stationId) {
    if (!isConnected()) return false;
    int effectiveStationId = stationId;
    if (effectiveStationId <= 0 &&
        AppConfig::instance().role() == AppRole::Client &&
        AppConfig::instance().stationId() > 0) {
        effectiveStationId = AppConfig::instance().stationId();
    }
    return m_stream->execNamed(
        "INSERT INTO audit_logs (log_timestamp, action_type, details, operator_username, station_id) "
        "VALUES (:ts, :act, :det, :usr, :st)",
        {{":ts", QDateTime::currentDateTime().toString(Qt::ISODate)},
         {":act", action},
         {":det", details},
         {":usr", user.isEmpty() ? AppConfig::instance().currentUsername() : user},
         {":st", effectiveStationId}}).ok;
}

QVector<AuditLogRecord> DbManager::getAuditLogs(int limit, const QString &search) {
    QVector<AuditLogRecord> list;
    if (!isConnected()) return list;
    QString sql = "SELECT id, log_timestamp, action_type, details, operator_username, station_id FROM audit_logs ";
    const QString needle = search.trimmed();
    QVariantMap binds;
    if (!needle.isEmpty()) {
        sql += "WHERE action_type LIKE :s1 OR details LIKE :s2 OR operator_username LIKE :s3 ";
        const QString pattern = "%" + needle + "%";
        binds.insert(":s1", pattern);
        binds.insert(":s2", pattern);
        binds.insert(":s3", pattern);
    }
    sql += QString("ORDER BY id DESC LIMIT %1").arg(qMax(10, limit));

    const DbQueryResult res = binds.isEmpty() ? m_stream->exec(sql) : m_stream->execNamed(sql, binds);
    if (res.ok) {
        for (const DbRow &row : res.rows) {
            AuditLogRecord r;
            r.id = row.at(0).toLongLong();
            r.timestamp = row.at(1).toDateTime();
            if (!r.timestamp.isValid()) {
                r.timestamp = QDateTime::fromString(row.at(1).toString(), Qt::ISODate);
            }
            r.actionType = row.at(2).toString();
            r.details = row.at(3).toString();
            r.operatorUsername = row.at(4).toString();
            r.stationId = row.at(5).toInt();
            list.append(r);
        }
    }
    return list;
}

bool DbManager::clearAuditLogs() {
    if (!isConnected()) return false;
    return m_stream->exec("DELETE FROM audit_logs").ok;
}

qint64 DbManager::insertTestRun(const TestRunData &run) {
    if (!isConnected()) {
        const AppConfig &cfg = AppConfig::instance();
        connectToDatabase(cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword());
    }
    if (!isConnected()) {
        m_lastError = "Veritabanı bağlantısı kurulamadığı için test kaydı eklenemedi.";
        qWarning() << "[DbManager]" << m_lastError;
        return -1;
    }
    m_stream->beginTransaction();

    int targetProfileId = run.satelliteProfileId;
    if (targetProfileId <= 0 && !run.profileName.trimmed().isEmpty()) {
        const DbQueryResult profRes = m_stream->execNamed(
            "SELECT id FROM satellite_profiles WHERE profile_name = :pname LIMIT 1",
            {{":pname", run.profileName.trimmed()}});
        if (profRes.ok && profRes.hasRows()) {
            targetProfileId = profRes.first().at(0).toInt();
        }
    }
    if (targetProfileId > 0) {
        const DbQueryResult chkRes = m_stream->execNamed(
            "SELECT id FROM satellite_profiles WHERE id = :pid LIMIT 1",
            {{":pid", targetProfileId}});
        if (!chkRes.ok || !chkRes.hasRows()) {
            targetProfileId = 0;
        }
    }
    if (targetProfileId <= 0) {
        const DbQueryResult minRes = m_stream->exec("SELECT MIN(id) FROM satellite_profiles");
        if (minRes.hasRows() && minRes.first().at(0).toInt() > 0) {
            targetProfileId = minRes.first().at(0).toInt();
        } else {
            ensureDefaultDataIfMissing();
            const DbQueryResult minRes2 = m_stream->exec("SELECT MIN(id) FROM satellite_profiles");
            targetProfileId = (minRes2.hasRows() && minRes2.first().at(0).toInt() > 0)
                                  ? minRes2.first().at(0).toInt()
                                  : 1;
        }
    }

    // Foreign key safety: set NULL if work order id does not exist in work_orders table
    QVariant woVal;
    if (run.workOrderId > 0) {
        const DbQueryResult woRes = m_stream->execNamed(
            "SELECT id FROM work_orders WHERE id = :wid LIMIT 1",
            {{":wid", run.workOrderId}});
        if (woRes.ok && woRes.hasRows()) {
            woVal = run.workOrderId;
        }
    }

    const QString opUser = run.operatorUsername.trimmed().isEmpty()
                               ? AppConfig::instance().currentUsername()
                               : run.operatorUsername.trimmed();

    QString oRes = run.overallResult.trimmed().toUpper();
    if (oRes != "PASS" && oRes != "ABORTED") {
        oRes = "FAIL";
    }

    QString sn = run.antennaSn.trimmed();
    if (sn.isEmpty()) {
        sn = "SN-UNKNOWN";
    }

    const QString ts = (run.timestamp.isValid() ? run.timestamp : QDateTime::currentDateTime()).toString("yyyy-MM-dd HH:mm:ss");
    const QString endTimeStr = (run.testEndTime.isValid() ? run.testEndTime : QDateTime::currentDateTime()).toString("yyyy-MM-dd HH:mm:ss");

    const DbQueryResult insRes = m_stream->execNamed(
        "INSERT INTO test_runs (work_order_id, station_id, operator_id, tester_name, operator_username, antenna_sn, "
        "satellite_profile_id, test_timestamp, duration_sec, overall_result, fail_reason, "
        "avg_delta_db, max_delta_db, notes, last_drx_device_time, test_end_time, dt_reserved_ms) "
        "VALUES (:wo, :st, :op, :tname, :opuser, :sn, :sp, :ts, :dur, :res, :fr, :avg, :max, :notes, :drx_time, :end_time, :dt_res)",
        {{":wo", woVal},
         {":st", run.stationId},
         {":op", run.operatorId > 0 ? run.operatorId : QVariant()},
         {":tname", run.testerName.left(100)},
         {":opuser", opUser.left(64)},
         {":sn", sn.left(64)},
         {":sp", targetProfileId},
         {":ts", ts},
         {":dur", run.durationSec},
         {":res", oRes},
         {":fr", run.failReason.trimmed()},
         {":avg", run.avgDeltaDb},
         {":max", run.maxDeltaDb},
         {":notes", run.notes},
         {":drx_time", run.lastDrxDeviceTime.left(64)},
         {":end_time", endTimeStr},
         {":dt_res", run.dtReservedMs}});

    if (!insRes.ok) {
        m_lastError = insRes.error;
        qWarning() << "[DbManager] test_runs insert failed:" << m_lastError;
        m_stream->rollbackTransaction();
        return -1;
    }

    const qint64 runId = insRes.lastInsertId;

    // Insert channel measurement results
    for (const auto &ch : run.channels) {
        QString chRes = ch.status.trimmed().toUpper();
        if (chRes != "PASS" && chRes != "WARNING") {
            chRes = "FAIL";
        }
        QString chPol = ch.polarization.trimmed().toUpper();
        if (chPol != "V" && chPol != "H") {
            chPol = "V";
        }

        const QString chSampleTime = (ch.sampleHostTime.isValid() ? ch.sampleHostTime : QDateTime::currentDateTime()).toString("yyyy-MM-dd HH:mm:ss");

        const DbQueryResult chIns = m_stream->execNamed(
            "INSERT INTO test_run_channels (test_run_id, channel_no, channel_name, frequency_mhz, "
            "symbol_rate, polarization, dut_cn_avg, dut_pow_avg, ref_cn_avg, ref_pow_avg, delta_db, "
            "carrier_locked, video_locked, audio_locked, tolerance_db, result, "
            "drx_device_time, sample_host_time, dt_reserved_ms) "
            "VALUES (:tr_id, :ch_no, :ch_name, :freq, :sr, :pol, :dut_cn, :dut_pow, :ref_cn, :ref_pow, "
            ":delta, :c_lock, :v_lock, :a_lock, :tol, :res, :ch_drx, :ch_host, :ch_dt)",
            {{":tr_id", runId},
             {":ch_no", ch.channelNo},
             {":ch_name", ch.channelName.left(64)},
             {":freq", ch.frequencyMhz},
             {":sr", ch.symbolRate},
             {":pol", chPol},
             {":dut_cn", ch.dutCn},
             {":dut_pow", ch.dutPow},
             {":ref_cn", ch.refCn},
             {":ref_pow", ch.refPow},
             {":delta", ch.deltaDb},
             {":c_lock", ch.carrierLocked ? 1 : 0},
             {":v_lock", ch.videoLocked ? 1 : 0},
             {":a_lock", ch.audioLocked ? 1 : 0},
             {":tol", ch.toleranceDb},
             {":res", chRes},
             {":ch_drx", ch.drxDeviceTime.left(64)},
             {":ch_host", chSampleTime},
             {":ch_dt", ch.dtReservedMs}});
        if (!chIns.ok) {
            qWarning() << "[DbManager] test_run_channels insert failed:" << chIns.error;
        }
    }

    m_stream->commitTransaction();

    if (run.workOrderId > 0) {
        incrementOrderCount(run.workOrderId, run.overallResult == "PASS");
    }

    logAudit("TEST_FINISHED",
             QString("Seri No: %1 | Sonuç: %2 | Ortalama Fark: %3 dB | Süre: %4 sn")
                 .arg(run.antennaSn, run.overallResult)
                 .arg(run.avgDeltaDb, 0, 'f', 2)
                 .arg(run.durationSec),
             opUser, run.stationId);

    return runId;
}

QVector<TestRunData> DbManager::getTestRuns(int limit, const QString &searchSn) {
    QVector<TestRunData> list;
    if (!isConnected()) return list;

    QString sql = "SELECT tr.id, tr.work_order_id, tr.station_id, tr.operator_id, tr.tester_name, "
                  "COALESCE(NULLIF(tr.operator_username, ''), u.username, '') AS op_username, "
                  "tr.antenna_sn, tr.satellite_profile_id, COALESCE(sp.profile_name, '') AS prof_name, "
                  "tr.test_timestamp, tr.duration_sec, tr.overall_result, tr.fail_reason, "
                  "tr.avg_delta_db, tr.max_delta_db, tr.notes, "
                  "tr.last_drx_device_time, tr.test_end_time, tr.dt_reserved_ms "
                  "FROM test_runs tr "
                  "LEFT JOIN users u ON u.id = tr.operator_id "
                  "LEFT JOIN satellite_profiles sp ON sp.id = tr.satellite_profile_id ";

    const QString needle = searchSn.trimmed();
    QVariantMap binds;
    if (!needle.isEmpty()) {
        sql += "WHERE tr.antenna_sn LIKE :s1 OR tr.tester_name LIKE :s2 "
               "OR tr.operator_username LIKE :s3 OR u.username LIKE :s4 "
               "OR sp.profile_name LIKE :s5 OR tr.fail_reason LIKE :s6 OR tr.notes LIKE :s7 ";
        const QString like = QString("%%1%").arg(needle);
        binds.insert(":s1", like);
        binds.insert(":s2", like);
        binds.insert(":s3", like);
        binds.insert(":s4", like);
        binds.insert(":s5", like);
        binds.insert(":s6", like);
        binds.insert(":s7", like);
    }
    sql += QString("ORDER BY tr.id DESC LIMIT %1").arg(qMax(1, limit));

    const DbQueryResult res = binds.isEmpty() ? m_stream->exec(sql) : m_stream->execNamed(sql, binds);
    if (!res.ok) {
        m_lastError = res.error;
        qWarning() << "[DbManager] getTestRuns failed:" << m_lastError;
        return list;
    }

    for (const DbRow &row : res.rows) {
        TestRunData r;
        r.id = row.at(0).toLongLong();
        r.workOrderId = row.at(1).toInt();
        r.stationId = row.at(2).toInt();
        r.operatorId = row.at(3).toInt();
        r.testerName = row.at(4).toString();
        r.operatorUsername = row.at(5).toString();
        r.antennaSn = row.at(6).toString();
        r.satelliteProfileId = row.at(7).toInt();
        r.profileName = row.at(8).toString();
        r.timestamp = QDateTime::fromString(row.at(9).toString(), Qt::ISODate);
        if (!r.timestamp.isValid()) {
            r.timestamp = row.at(9).toDateTime();
        }
        r.durationSec = row.at(10).toInt();
        r.overallResult = row.at(11).toString();
        r.failReason = row.at(12).toString();
        r.avgDeltaDb = row.at(13).toDouble();
        r.maxDeltaDb = row.at(14).toDouble();
        r.notes = row.at(15).toString();
        r.lastDrxDeviceTime = row.at(16).toString();
        r.testEndTime = row.at(17).toDateTime();
        r.dtReservedMs = row.at(18).toLongLong();
        list.append(r);
    }
    return list;
}

int DbManager::getTestRunsCount(const QString &searchNeedle, int stationFilter, const QString &resultFilter) {
    if (!isConnected()) return 0;

    QString sql = "SELECT COUNT(*) FROM test_runs tr "
                  "LEFT JOIN users u ON u.id = tr.operator_id "
                  "LEFT JOIN satellite_profiles sp ON sp.id = tr.satellite_profile_id WHERE 1=1 ";
    QVariantMap binds;

    if (stationFilter != -1) {
        sql += "AND tr.station_id = :st ";
        binds.insert(":st", stationFilter);
    }
    if (!resultFilter.trimmed().isEmpty()) {
        sql += "AND tr.overall_result = :res ";
        binds.insert(":res", resultFilter.trimmed());
    }
    const QString needle = searchNeedle.trimmed();
    if (!needle.isEmpty()) {
        sql += "AND (tr.antenna_sn LIKE :s1 OR tr.tester_name LIKE :s2 "
               "OR tr.operator_username LIKE :s3 OR u.username LIKE :s4 "
               "OR sp.profile_name LIKE :s5 OR tr.fail_reason LIKE :s6 OR tr.notes LIKE :s7) ";
        const QString like = QString("%%1%").arg(needle);
        binds.insert(":s1", like);
        binds.insert(":s2", like);
        binds.insert(":s3", like);
        binds.insert(":s4", like);
        binds.insert(":s5", like);
        binds.insert(":s6", like);
        binds.insert(":s7", like);
    }

    const DbQueryResult res = binds.isEmpty() ? m_stream->exec(sql) : m_stream->execNamed(sql, binds);
    if (res.ok && res.hasRows()) {
        return res.first().at(0).toInt();
    }
    return 0;
}

QVector<TestRunData> DbManager::getTestRunsPaged(int page, int pageSize, const QString &searchNeedle, int stationFilter, const QString &resultFilter) {
    QVector<TestRunData> list;
    if (!isConnected()) return list;

    int safePage = qMax(1, page);
    int safePageSize = qBound(10, pageSize, 1000);
    int offset = (safePage - 1) * safePageSize;

    QString sql = "SELECT tr.id, tr.work_order_id, tr.station_id, tr.operator_id, tr.tester_name, "
                  "COALESCE(NULLIF(tr.operator_username, ''), u.username, '') AS op_username, "
                  "tr.antenna_sn, tr.satellite_profile_id, COALESCE(sp.profile_name, '') AS prof_name, "
                  "tr.test_timestamp, tr.duration_sec, tr.overall_result, tr.fail_reason, "
                  "tr.avg_delta_db, tr.max_delta_db, tr.notes, "
                  "tr.last_drx_device_time, tr.test_end_time, tr.dt_reserved_ms "
                  "FROM test_runs tr "
                  "LEFT JOIN users u ON u.id = tr.operator_id "
                  "LEFT JOIN satellite_profiles sp ON sp.id = tr.satellite_profile_id WHERE 1=1 ";
    QVariantMap binds;

    if (stationFilter != -1) {
        sql += "AND tr.station_id = :st ";
        binds.insert(":st", stationFilter);
    }
    if (!resultFilter.trimmed().isEmpty()) {
        sql += "AND tr.overall_result = :res ";
        binds.insert(":res", resultFilter.trimmed());
    }
    const QString needle = searchNeedle.trimmed();
    if (!needle.isEmpty()) {
        sql += "AND (tr.antenna_sn LIKE :s1 OR tr.tester_name LIKE :s2 "
               "OR tr.operator_username LIKE :s3 OR u.username LIKE :s4 "
               "OR sp.profile_name LIKE :s5 OR tr.fail_reason LIKE :s6 OR tr.notes LIKE :s7) ";
        const QString like = QString("%%1%").arg(needle);
        binds.insert(":s1", like);
        binds.insert(":s2", like);
        binds.insert(":s3", like);
        binds.insert(":s4", like);
        binds.insert(":s5", like);
        binds.insert(":s6", like);
        binds.insert(":s7", like);
    }
    sql += QString("ORDER BY tr.id DESC LIMIT %1 OFFSET %2").arg(safePageSize).arg(offset);

    const DbQueryResult res = binds.isEmpty() ? m_stream->exec(sql) : m_stream->execNamed(sql, binds);
    if (!res.ok) {
        m_lastError = res.error;
        qWarning() << "[DbManager] getTestRunsPaged failed:" << m_lastError;
        return list;
    }

    for (const DbRow &row : res.rows) {
        TestRunData r;
        r.id = row.at(0).toLongLong();
        r.workOrderId = row.at(1).toInt();
        r.stationId = row.at(2).toInt();
        r.operatorId = row.at(3).toInt();
        r.testerName = row.at(4).toString();
        r.operatorUsername = row.at(5).toString();
        r.antennaSn = row.at(6).toString();
        r.satelliteProfileId = row.at(7).toInt();
        r.profileName = row.at(8).toString();
        r.timestamp = QDateTime::fromString(row.at(9).toString(), Qt::ISODate);
        if (!r.timestamp.isValid()) {
            r.timestamp = row.at(9).toDateTime();
        }
        r.durationSec = row.at(10).toInt();
        r.overallResult = row.at(11).toString();
        r.failReason = row.at(12).toString();
        r.avgDeltaDb = row.at(13).toDouble();
        r.maxDeltaDb = row.at(14).toDouble();
        r.notes = row.at(15).toString();
        r.lastDrxDeviceTime = row.at(16).toString();
        r.testEndTime = row.at(17).toDateTime();
        r.dtReservedMs = row.at(18).toLongLong();
        list.append(r);
    }
    return list;
}

QVector<ChannelSampleResult> DbManager::getTestRunChannels(qint64 testRunId) {
    QVector<ChannelSampleResult> list;
    if (!isConnected() || testRunId <= 0) return list;

    const DbQueryResult res = m_stream->execNamed(
        "SELECT channel_no, channel_name, frequency_mhz, symbol_rate, polarization, "
        "dut_cn_avg, dut_pow_avg, ref_cn_avg, ref_pow_avg, delta_db, "
        "carrier_locked, video_locked, audio_locked, tolerance_db, result, "
        "drx_device_time, sample_host_time, dt_reserved_ms "
        "FROM test_run_channels WHERE test_run_id = :id ORDER BY channel_no ASC",
        {{":id", testRunId}});
    if (!res.ok) {
        qWarning() << "[DbManager] getTestRunChannels failed:" << res.error;
        return list;
    }

    for (const DbRow &row : res.rows) {
        ChannelSampleResult ch;
        ch.channelNo = row.at(0).toInt();
        ch.channelName = row.at(1).toString();
        ch.frequencyMhz = row.at(2).toInt();
        ch.symbolRate = row.at(3).toInt();
        ch.polarization = row.at(4).toString();
        ch.dutCn = row.at(5).toDouble();
        ch.dutPow = row.at(6).toDouble();
        ch.refCn = row.at(7).toDouble();
        ch.refPow = row.at(8).toDouble();
        ch.deltaDb = row.at(9).toDouble();
        ch.carrierLocked = row.at(10).toInt() != 0;
        ch.videoLocked = row.at(11).toInt() != 0;
        ch.audioLocked = row.at(12).toInt() != 0;
        ch.toleranceDb = row.at(13).toDouble();
        ch.status = row.at(14).toString();
        ch.drxDeviceTime = row.at(15).toString();
        ch.sampleHostTime = row.at(16).toDateTime();
        ch.dtReservedMs = row.at(17).toLongLong();
        list.append(ch);
    }
    return list;
}

bool DbManager::deleteTestRun(qint64 testRunId) {
    if (!isConnected() || testRunId <= 0) return false;
    m_stream->execNamed("DELETE FROM test_run_channels WHERE test_run_id = :id", {{":id", testRunId}});
    const DbQueryResult res = m_stream->execNamed("DELETE FROM test_runs WHERE id = :id", {{":id", testRunId}});
    if (!res.ok) {
        m_lastError = res.error;
        return false;
    }
    return true;
}

QVector<double> DbManager::getDeltaDbValuesForSpc(int workOrderId, int stationId, int profileId, int limit) {
    QVector<double> values;
    if (!isConnected()) return values;

    QString sql = "SELECT avg_delta_db FROM test_runs WHERE overall_result != 'ABORTED' ";
    QVariantMap binds;

    if (workOrderId > 0) {
        sql += "AND work_order_id = :wo ";
        binds.insert(":wo", workOrderId);
    }
    if (stationId > 0) {
        sql += "AND station_id = :st ";
        binds.insert(":st", stationId);
    }
    if (profileId > 0) {
        sql += "AND satellite_profile_id = :sp ";
        binds.insert(":sp", profileId);
    }

    sql += QString("ORDER BY id DESC LIMIT %1").arg(qBound(10, limit, 5000));

    const DbQueryResult res = binds.isEmpty() ? m_stream->exec(sql) : m_stream->execNamed(sql, binds);
    if (res.ok) {
        for (const DbRow &row : res.rows) {
            values.append(row.at(0).toDouble());
        }
        std::reverse(values.begin(), values.end());
    } else {
        qWarning() << "[DbManager] getDeltaDbValuesForSpc failed:" << res.error;
    }
    return values;
}

bool DbManager::testRunExists(const QString &antennaSn, int stationId, const QDateTime &timestamp) {
    if (!isConnected()) return false;
    const DbQueryResult res = m_stream->execNamed(
        "SELECT id FROM test_runs WHERE antenna_sn = :sn AND station_id = :st AND test_timestamp = :ts LIMIT 1",
        {{":sn", antennaSn}, {":st", stationId}, {":ts", timestamp.toString(Qt::ISODate)}});
    return (res.ok && res.hasRows());
}

QString DbManager::getSetting(const QString &key, const QString &defaultValue) {
    if (!isConnected()) return defaultValue;
    const DbQueryResult res = m_stream->execNamed(
        "SELECT setting_value FROM system_settings WHERE setting_key = :k",
        {{":k", key}});
    if (res.ok && res.hasRows()) {
        return res.first().at(0).toString();
    }
    return defaultValue;
}

bool DbManager::setSetting(const QString &key, const QString &value, const QString &desc) {
    if (!isConnected()) return false;
    const QString sql = isUsingSqlite()
        ? "INSERT OR REPLACE INTO system_settings (setting_key, setting_value, description) VALUES (:k, :v, :d)"
        : "INSERT INTO system_settings (setting_key, setting_value, description) "
          "VALUES (:k, :v, :d) ON DUPLICATE KEY UPDATE setting_value = VALUES(setting_value), description = VALUES(description)";
    return m_stream->execNamed(sql, {{":k", key}, {":v", value}, {":d", desc}}).ok;
}

bool DbManager::tryAcquireDbLock(int stationId, int &outLockedByStation, int &outCurrentChannel) {
    outLockedByStation = 0;
    outCurrentChannel = 0;
    if (!isConnected()) return false;

    const QString sql = isUsingSqlite()
        ? "UPDATE reference_lock SET locked_by_station = :sid WHERE id = 1 AND (locked_by_station = 0 OR locked_by_station = :sid2)"
        : "UPDATE reference_lock "
          "SET locked_by_station = :sid, "
          "    lock_acquired_at = CURRENT_TIMESTAMP, "
          "    heartbeat_at = CURRENT_TIMESTAMP "
          "WHERE id = 1 AND (locked_by_station = 0 OR locked_by_station = :sid2 "
          "      OR TIMESTAMPDIFF(SECOND, heartbeat_at, NOW()) > 45)";

    const DbQueryResult res = m_stream->execNamed(sql, {{":sid", stationId}, {":sid2", stationId}});
    if (res.ok && res.rowsAffected > 0) {
        outLockedByStation = stationId;
        return true;
    }

    // Lock busy; identify active holding station and channel
    const DbQueryResult holdRes = m_stream->exec("SELECT locked_by_station, current_channel_no FROM reference_lock WHERE id = 1");
    if (holdRes.ok && holdRes.hasRows()) {
        outLockedByStation = holdRes.first().at(0).toInt();
        outCurrentChannel = holdRes.first().at(1).toInt();
    }
    return false;
}

bool DbManager::releaseDbLock(int stationId) {
    if (!isConnected()) return false;
    return m_stream->execNamed(
        "UPDATE reference_lock SET locked_by_station = 0, current_channel_no = 0 "
        "WHERE id = 1 AND (locked_by_station = :sid OR :sid2 <= 0)",
        {{":sid", stationId}, {":sid2", stationId}}).ok;
}

bool DbManager::updateDbLockHeartbeat(int stationId, int currentChannel) {
    if (!isConnected()) return false;
    const QString sql = isUsingSqlite()
        ? "UPDATE reference_lock SET current_channel_no = :ch WHERE id = 1 AND locked_by_station = :sid"
        : "UPDATE reference_lock SET heartbeat_at = CURRENT_TIMESTAMP, current_channel_no = :ch "
          "WHERE id = 1 AND locked_by_station = :sid";
    return m_stream->execNamed(sql, {{":ch", currentChannel}, {":sid", stationId}}).ok;
}

int DbManager::getDbLockHolder(int &outCurrentChannel) {
    outCurrentChannel = 0;
    if (!isConnected()) return 0;
    if (isUsingSqlite()) {
        const DbQueryResult res = m_stream->exec("SELECT locked_by_station, current_channel_no FROM reference_lock WHERE id = 1");
        if (res.ok && res.hasRows()) {
            outCurrentChannel = res.first().at(1).toInt();
            return res.first().at(0).toInt();
        }
    } else {
        const DbQueryResult res = m_stream->exec("SELECT locked_by_station, current_channel_no, TIMESTAMPDIFF(SECOND, heartbeat_at, NOW()) FROM reference_lock WHERE id = 1");
        if (res.ok && res.hasRows()) {
            const int holder = res.first().at(0).toInt();
            outCurrentChannel = res.first().at(1).toInt();
            const int ageSec = res.first().at(2).toInt();
            if (ageSec > 45) {
                return 0; // Lock lease expired
            }
            return holder;
        }
    }
    return 0;
}

bool DbManager::startPortableMariaDbIfPresent() {
    ensureDefaultStream();
    return m_stream->startLocalService();
}

void DbManager::stopPortableMariaDbIfRunning() {
    if (m_stream) {
        m_stream->stopLocalService();
    }
}

namespace {

// Constant-time byte array comparison to prevent timing side-channel attacks
bool constantTimeEquals(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

// Generates a 256-bit keystream block for CTR-mode payload encryption
QByteArray vaultKeystreamBlock(const QByteArray &masterKey, const QByteArray &iv, quint32 counter) {
    QByteArray blockInput = iv;
    blockInput.append(static_cast<char>((counter >> 24) & 0xFF));
    blockInput.append(static_cast<char>((counter >> 16) & 0xFF));
    blockInput.append(static_cast<char>((counter >> 8) & 0xFF));
    blockInput.append(static_cast<char>(counter & 0xFF));
    return QMessageAuthenticationCode::hash(blockInput, masterKey, QCryptographicHash::Sha256);
}

QByteArray vaultCryptCtr(const QByteArray &input, const QByteArray &masterKey, const QByteArray &iv) {
    QByteArray output(input.size(), Qt::Uninitialized);
    quint32 counter = 0;
    int offset = 0;
    while (offset < input.size()) {
        const QByteArray ks = vaultKeystreamBlock(masterKey, iv, counter++);
        const int chunk = qMin(ks.size(), input.size() - offset);
        for (int i = 0; i < chunk; ++i) {
            output[offset + i] = input[offset + i] ^ ks[i];
        }
        offset += chunk;
    }
    return output;
}

QByteArray vaultEncryptionKey() {
    return QCryptographicHash::hash(
        QByteArrayLiteral("NETA::DRX9000::LOCAL_VAULT_ENC_KEY::2026::v1"),
        QCryptographicHash::Sha256);
}

QByteArray vaultMacKey() {
    return QCryptographicHash::hash(
        QByteArrayLiteral("NETA::DRX9000::LOCAL_VAULT_HMAC_KEY::2026::v1"),
        QCryptographicHash::Sha256);
}

} // namespace

QString DbManager::deriveUserPasswordToken(const QString &username, const QString &passwordOrToken) {
    const QString trimmedPass = passwordOrToken.trimmed();
    if (trimmedPass.startsWith(QStringLiteral("$v1$")) && trimmedPass.size() == 68) {
        return trimmedPass;
    }

    const QByteArray userSalt = QMessageAuthenticationCode::hash(
        username.trimmed().toLower().toUtf8(),
        QByteArrayLiteral("NETA::DRX9000::USER_PWD_SALT::2026"),
        QCryptographicHash::Sha256);

    QByteArray digest = trimmedPass.toUtf8();
    for (int i = 0; i < 4096; ++i) {
        digest = QMessageAuthenticationCode::hash(digest, userSalt, QCryptographicHash::Sha256);
    }
    return QStringLiteral("$v1$") + QString::fromLatin1(digest.toHex());
}

QString DbManager::localUserVaultPath() const {
    const QString appDir = QCoreApplication::instance()
                               ? QCoreApplication::applicationDirPath()
                               : QDir::currentPath();
    return QDir(appDir).filePath(QStringLiteral("neta_auth.vault"));
}

bool DbManager::syncLocalUserVault(const QVector<UserRecord> &users) {
    if (users.isEmpty()) return false;

    // 1. Serialize user records with one-way derived password tokens into binary buffer
    QByteArray plainPayload;
    {
        QDataStream ds(&plainPayload, QIODevice::WriteOnly);
        ds.setVersion(QDataStream::Qt_5_15);
        ds << static_cast<quint32>(0x4E565531); // Inner payload magic 'NVU1'
        ds << static_cast<qint32>(users.size());
        for (const UserRecord &u : users) {
            const QString uname = u.username.trimmed();
            const QString token = deriveUserPasswordToken(
                uname, u.password.isEmpty() ? QStringLiteral("123") : u.password);
            ds << static_cast<qint32>(u.id)
               << uname
               << token
               << u.fullName.trimmed()
               << u.role.trimmed().toUpper()
               << u.isActive;
        }
    }

    // 2. Generate 16-byte cryptographic random IV/salt
    QByteArray iv(16, Qt::Uninitialized);
    for (int i = 0; i < 16; i += 4) {
        const quint32 r = QRandomGenerator::system()->generate();
        iv[i]     = static_cast<char>((r >> 24) & 0xFF);
        iv[i + 1] = static_cast<char>((r >> 16) & 0xFF);
        iv[i + 2] = static_cast<char>((r >> 8) & 0xFF);
        iv[i + 3] = static_cast<char>(r & 0xFF);
    }

    // 3. Encrypt payload with SHA-256 CTR stream cipher
    const QByteArray cipherPayload = vaultCryptCtr(plainPayload, vaultEncryptionKey(), iv);

    // 4. Assemble header + IV + ciphertext and compute 32-byte HMAC-SHA256 integrity tag
    QByteArray body;
    body.reserve(4 + 16 + cipherPayload.size());
    body.append("NVA1", 4);
    body.append(iv);
    body.append(cipherPayload);

    const QByteArray macTag = QMessageAuthenticationCode::hash(
        body, vaultMacKey(), QCryptographicHash::Sha256);

    QFile file(localUserVaultPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(body);
    file.write(macTag);
    file.flush();
    file.close();
    return true;
}

bool DbManager::loadLocalUserVault(QVector<UserRecord> &outUsers) const {
    outUsers.clear();
    QFile file(localUserVaultPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    // Minimum size: 4B magic + 16B IV + 8B min payload + 32B HMAC = 60 bytes
    if (raw.size() < 60 || raw.left(4) != QByteArrayLiteral("NVA1")) {
        return false;
    }

    const QByteArray body = raw.left(raw.size() - 32);
    const QByteArray storedMac = raw.right(32);
    const QByteArray computedMac = QMessageAuthenticationCode::hash(
        body, vaultMacKey(), QCryptographicHash::Sha256);

    if (!constantTimeEquals(storedMac, computedMac)) {
        qWarning() << "[DbManager] Local user vault HMAC integrity verification failed (tampered file rejected).";
        return false;
    }

    const QByteArray iv = body.mid(4, 16);
    const QByteArray cipherPayload = body.mid(20);
    const QByteArray plainPayload = vaultCryptCtr(cipherPayload, vaultEncryptionKey(), iv);

    QDataStream ds(plainPayload);
    ds.setVersion(QDataStream::Qt_5_15);
    quint32 innerMagic = 0;
    qint32 count = 0;
    ds >> innerMagic >> count;
    if (innerMagic != 0x4E565531 || count <= 0 || count > 1000) {
        return false;
    }

    for (qint32 i = 0; i < count; ++i) {
        qint32 id = 0;
        QString uname, token, fullName, role;
        bool active = true;
        ds >> id >> uname >> token >> fullName >> role >> active;
        if (ds.status() != QDataStream::Ok) {
            outUsers.clear();
            return false;
        }
        UserRecord u;
        u.id = id;
        u.username = uname;
        u.password = token;
        u.fullName = fullName;
        u.role = role;
        u.isActive = active;
        outUsers.append(u);
    }
    return !outUsers.isEmpty();
}

bool DbManager::authenticateUser(const QString &username, const QString &password, UserRole &outRole, QString &outFullName) {
    const QString trimmedUser = username.trimmed();
    const QString trimmedPass = password.trimmed();

    if (trimmedUser.isEmpty() || trimmedPass.isEmpty()) {
        return false;
    }

    // 1. If central MariaDB is connected, verify directly against the users table and refresh local vault
    if (isConnected()) {
        const DbQueryResult res = m_stream->execNamed(
            "SELECT role, full_name, password_hash, is_active FROM users WHERE username = :u LIMIT 1;",
            {{":u", trimmedUser}});
        if (res.ok && res.hasRows()) {
            const DbRow &row = res.first();
            const bool isActive = row.value("is_active").toInt() != 0;
            if (!isActive) {
                getUsers(); // Sync vault so deactivated state is cached locally
                return false;
            }
            const QString roleStr = row.value("role").toString().trimmed().toUpper();
            const QString fullName = row.value("full_name").toString();
            const QString passHash = row.value("password_hash").toString();

            const QString inputToken = deriveUserPasswordToken(trimmedUser, trimmedPass);
            const QString dbToken = deriveUserPasswordToken(trimmedUser, passHash.isEmpty() ? QStringLiteral("123") : passHash);

            if (trimmedPass == passHash || constantTimeEquals(inputToken.toLatin1(), dbToken.toLatin1())) {
                outRole = (roleStr == "ADMIN") ? UserRole::Engineer : UserRole::Technician;
                outFullName = fullName.isEmpty() ? trimmedUser : fullName;
                getUsers(); // Keep local encrypted vault synchronized
                return true;
            }
            return false;
        }
    }

    // 2. Central MariaDB is offline: verify against this PC's encrypted & HMAC-signed local user vault
    QVector<UserRecord> vaultUsers;
    if (loadLocalUserVault(vaultUsers)) {
        for (const UserRecord &u : vaultUsers) {
            if (u.username.compare(trimmedUser, Qt::CaseInsensitive) == 0) {
                if (!u.isActive) {
                    return false; // Deactivated user cannot log in offline
                }
                const QString expectedToken = deriveUserPasswordToken(u.username, u.password);
                const QString candidateToken = deriveUserPasswordToken(u.username, trimmedPass);
                if (constantTimeEquals(expectedToken.toLatin1(), candidateToken.toLatin1())) {
                    outRole = (u.role.compare("ADMIN", Qt::CaseInsensitive) == 0)
                                  ? UserRole::Engineer
                                  : UserRole::Technician;
                    outFullName = u.fullName.isEmpty() ? u.username : u.fullName;
                    return true;
                }
                return false;
            }
        }
        // Vault exists and is authoritative for this PC: unknown users are rejected
        return false;
    }

    // 3. First-boot factory fallback (only used before the first database/host sync creates neta_auth.vault)
    const QVector<UserRecord> factoryDefaults = {
        {1, "admin",     "123", "Sistem Yöneticisi / Başmühendis", "ADMIN",    true},
        {2, "engineer",  "123", "Ar-Ge Test Mühendisi",            "ADMIN",    true},
        {3, "operator1", "123", "Masa 1 Operatörü",                "OPERATOR", true},
        {4, "operator2", "123", "Masa 2 Operatörü",                "OPERATOR", true},
        {5, "operator3", "123", "Masa 3 Operatörü",                "OPERATOR", true},
        {6, "tekniker",  "123", "Saha Test Teknikeri",             "OPERATOR", true}
    };

    for (const UserRecord &def : factoryDefaults) {
        if (def.username.compare(trimmedUser, Qt::CaseInsensitive) == 0 && trimmedPass == def.password) {
            outRole = (def.role == "ADMIN") ? UserRole::Engineer : UserRole::Technician;
            outFullName = def.fullName;
            syncLocalUserVault(factoryDefaults);
            return true;
        }
    }

    return false;
}

QVector<UserRecord> DbManager::getUsers() {
    QVector<UserRecord> list;
    if (!isConnected()) {
        if (!loadLocalUserVault(list)) {
            list = {
                {1, "admin",     "123", "Sistem Yöneticisi / Başmühendis", "ADMIN",    true},
                {2, "engineer",  "123", "Ar-Ge Test Mühendisi",            "ADMIN",    true},
                {3, "operator1", "123", "Masa 1 Operatörü",                "OPERATOR", true},
                {4, "operator2", "123", "Masa 2 Operatörü",                "OPERATOR", true},
                {5, "operator3", "123", "Masa 3 Operatörü",                "OPERATOR", true},
                {6, "tekniker",  "123", "Saha Test Teknikeri",             "OPERATOR", true}
            };
            syncLocalUserVault(list);
            loadLocalUserVault(list);
        }
        return list;
    }

    const DbQueryResult res = m_stream->exec("SELECT id, username, password_hash, full_name, role, is_active FROM users ORDER BY id ASC;");
    for (const DbRow &row : res.rows) {
        UserRecord u;
        u.id = row.at(0).toInt();
        u.username = row.at(1).toString();
        u.password = row.at(2).toString();
        u.fullName = row.at(3).toString();
        u.role = row.at(4).toString().trimmed().toUpper();
        u.isActive = row.at(5).toInt() != 0;
        list.append(u);
    }
    if (!list.isEmpty()) {
        syncLocalUserVault(list);
    }
    return list;
}

bool DbManager::saveUser(const UserRecord &user) {
    const QString uname = user.username.trimmed();
    if (uname.isEmpty()) {
        m_lastError = "Kullanıcı adı boş olamaz.";
        return false;
    }

    if (!isConnected()) {
        // Offline mode: update local encrypted user vault directly so login & LAN sync still work
        QVector<UserRecord> vaultUsers = getUsers();
        bool found = false;
        int maxId = 0;
        for (UserRecord &existing : vaultUsers) {
            if (existing.id > maxId) maxId = existing.id;
            if ((user.id > 0 && existing.id == user.id) ||
                existing.username.compare(uname, Qt::CaseInsensitive) == 0) {
                existing.username = uname;
                existing.fullName = user.fullName.trimmed().isEmpty() ? uname : user.fullName.trimmed();
                existing.role = (user.role.trimmed().toUpper() == "ADMIN") ? QStringLiteral("ADMIN") : QStringLiteral("OPERATOR");
                existing.isActive = user.isActive;
                if (!user.password.isEmpty()) {
                    existing.password = deriveUserPasswordToken(uname, user.password);
                }
                found = true;
                break;
            }
        }
        if (!found) {
            UserRecord added;
            added.id = maxId + 1;
            added.username = uname;
            added.fullName = user.fullName.trimmed().isEmpty() ? uname : user.fullName.trimmed();
            added.role = (user.role.trimmed().toUpper() == "ADMIN") ? QStringLiteral("ADMIN") : QStringLiteral("OPERATOR");
            added.isActive = user.isActive;
            added.password = deriveUserPasswordToken(uname, user.password.isEmpty() ? QStringLiteral("123") : user.password);
            vaultUsers.append(added);
        }
        return syncLocalUserVault(vaultUsers);
    }

    QString sql;
    QVariantMap binds = {
        {":u", uname},
        {":fn", user.fullName.trimmed().isEmpty() ? uname : user.fullName.trimmed()},
        {":r", user.role.trimmed().toUpper() == "ADMIN" ? QString("ADMIN") : QString("OPERATOR")},
        {":act", user.isActive ? 1 : 0}
    };

    if (user.id <= 0) {
        sql = "INSERT INTO users (username, password_hash, full_name, role, is_active) VALUES (:u, :p, :fn, :r, :act);";
        binds.insert(":p", user.password.isEmpty() ? QString("123") : user.password);
    } else if (!user.password.isEmpty() && !user.password.startsWith(QStringLiteral("$v1$"))) {
        sql = "UPDATE users SET username = :u, password_hash = :p, full_name = :fn, role = :r, is_active = :act WHERE id = :id;";
        binds.insert(":p", user.password);
        binds.insert(":id", user.id);
    } else {
        sql = "UPDATE users SET username = :u, full_name = :fn, role = :r, is_active = :act WHERE id = :id;";
        binds.insert(":id", user.id);
    }

    const DbQueryResult res = m_stream->execNamed(sql, binds);
    if (!res.ok) {
        m_lastError = res.error;
        return false;
    }
    // Synchronize local encrypted user vault immediately after user change
    getUsers();
    logAudit(user.id <= 0 ? "USER_CREATED" : "USER_UPDATED",
             QString("Kullanıcı: %1 (Rol: %2, Aktif: %3)")
                 .arg(uname,
                      user.role.trimmed().toUpper() == "ADMIN" ? "ADMIN" : "OPERATOR",
                      user.isActive ? "Evet" : "Hayır"),
             AppConfig::instance().currentUsername(), 0);
    return true;
}

bool DbManager::deleteUser(int userId) {
    if (userId <= 0) return false;

    if (!isConnected()) {
        QVector<UserRecord> vaultUsers = getUsers();
        for (int i = 0; i < vaultUsers.size(); ++i) {
            if (vaultUsers[i].id == userId) {
                if (vaultUsers[i].username.compare("admin", Qt::CaseInsensitive) == 0) {
                    m_lastError = "Ana 'admin' hesabı silinemez.";
                    return false;
                }
                vaultUsers.removeAt(i);
                return syncLocalUserVault(vaultUsers);
            }
        }
        return false;
    }

    const DbQueryResult chk = m_stream->execNamed("SELECT username FROM users WHERE id = :id;", {{":id", userId}});
    if (chk.ok && chk.hasRows()) {
        if (chk.first().at(0).toString().compare("admin", Qt::CaseInsensitive) == 0) {
            m_lastError = "Ana 'admin' hesabı silinemez.";
            return false;
        }
    }

    const DbQueryResult res = m_stream->execNamed("DELETE FROM users WHERE id = :id;", {{":id", userId}});
    if (!res.ok) {
        m_lastError = res.error;
        return false;
    }
    // Synchronize local encrypted user vault immediately after user deletion
    getUsers();
    return true;
}

