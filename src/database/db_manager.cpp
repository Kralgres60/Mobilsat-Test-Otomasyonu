/**
 * @file db_manager.cpp
 * @brief Implementation of MariaDB/MySQL database management and distributed locking.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "db_manager.h"
#include "core/app_config.h"
#include <QDebug>
#include <QVariant>
#include <QSqlRecord>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStringList>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QElapsedTimer>

DbManager& DbManager::instance() {
    static DbManager s_instance;
    return s_instance;
}

DbManager::DbManager(QObject *parent) : QObject(parent) {
}

DbManager::~DbManager() {
    closeDatabase();
    stopPortableMariaDbIfRunning();
}

bool DbManager::connectToDatabase(const QString &host, int port, const QString &dbName,
                                  const QString &user, const QString &password) {
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    qInfo() << "[Database Service] Available SQL drivers on system:" << QSqlDatabase::drivers();

    // Driver selection: Prefer native QMYSQL, fallback to QODBC, and SQLite for testing
    if (QSqlDatabase::isDriverAvailable("QMYSQL")) {
        m_driverName = "QMYSQL";
    } else if (QSqlDatabase::isDriverAvailable("QODBC")) {
        m_driverName = "QODBC";
    } else {
        m_driverName = "QSQLITE";
    }

    QString effectiveUser = user.isEmpty() ? "root" : user;
    QString effectivePassword = password.isEmpty() ? "neta123" : password;

    QSqlDatabase db = QSqlDatabase::addDatabase(m_driverName, m_connectionName);

    if (m_driverName == "QMYSQL") {
        db.setHostName(host);
        db.setPort(port);
        db.setDatabaseName(dbName);
        db.setUserName(effectiveUser);
        db.setPassword(effectivePassword);
        db.setConnectOptions("MYSQL_OPT_SET_CHARSET_NAME=utf8mb4");
    } else if (m_driverName == "QODBC") {
        // MySQL ODBC DSN-less connection string
        QString dsn = QString("DRIVER={MySQL ODBC 8.0 Driver};SERVER=%1;PORT=%2;DATABASE=%3;UID=%4;PWD=%5;")
                          .arg(host).arg(port).arg(dbName).arg(effectiveUser).arg(effectivePassword);
        db.setDatabaseName(dsn);
    } else {
        // Local SQLite fallback mode
        db.setDatabaseName("neta_local_fallback.db");
    }

    if (!db.open()) {
        m_lastError = db.lastError().text();

        // If local connection failed, attempt to start bundled Portable MariaDB
        if (m_driverName == "QMYSQL" && (host == "127.0.0.1" || host.compare("localhost", Qt::CaseInsensitive) == 0)) {
            qInfo() << "[DbManager] Local connection failed; attempting to start bundled Portable MariaDB...";
            if (startPortableMariaDbIfPresent()) {
                if (db.open()) {
                    m_lastError.clear();
                } else {
                    m_lastError = db.lastError().text();
                }
            }
        }

        // If credentials failed with QMYSQL, attempt default factory fallback credentials
        if (m_driverName == "QMYSQL") {
            if (m_lastError.contains("Access denied", Qt::CaseInsensitive) || 
                m_lastError.contains("auth_gssapi_client", Qt::CaseInsensitive)) {
                qInfo() << "[DbManager] Connection rejected; attempting factory credentials (root / neta123)...";
                db.close();
                db.setUserName("root");
                db.setPassword("neta123");
                if (db.open()) {
                    m_lastError.clear();
                } else {
                    // Try secondary account
                    db.close();
                    db.setUserName("neta");
                    db.setPassword("neta123");
                    if (db.open()) {
                        m_lastError.clear();
                    }
                }
            }
        }

        // If database does not exist (Unknown database), connect to server root and provision it
        if (m_driverName == "QMYSQL" && (m_lastError.contains("Unknown database", Qt::CaseInsensitive) || db.lastError().nativeErrorCode() == "1049")) {
            qInfo() << "[DbManager] Database '" << dbName << "' not found; provisioning automatically...";
            db.close();
            db.setDatabaseName(""); // Connect to root server
            if (db.open()) {
                QSqlQuery qCreate(db);
                qCreate.exec(QString("CREATE DATABASE IF NOT EXISTS `%1` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;").arg(dbName));
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
            if (m_lastError.contains("Driver not loaded", Qt::CaseInsensitive)) {
                m_lastError = QString("Veritabanı sürücüsü yüklenemedi (%1). libmysql.dll veya 32-bit vcruntime140.dll eksik olabilir. [%2]")
                                  .arg(m_driverName, m_lastError);
            }
            qWarning() << "[Database Service] Unable to open database:" << m_lastError;
            emit connectionStatusChanged(false, m_lastError);
            return false;
        }
    }

    qDebug() << QString("[Database Service] Connected successfully to %1:%2 (Driver: %3)").arg(host).arg(port).arg(m_driverName);
    if (m_driverName == "QMYSQL" || m_driverName == "QODBC") {
        QSqlQuery qUtf(db);
        qUtf.exec("SET NAMES 'utf8mb4'");
        qUtf.exec("SET CHARACTER SET utf8mb4");
    }
    if (m_driverName == "QSQLITE") {
        initTablesIfSqlite();
    }
    emit connectionStatusChanged(true, "Connected (" + m_driverName + ")");
    return true;
}

void DbManager::closeDatabase() {
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
}

bool DbManager::isConnected() const {
    if (!QSqlDatabase::contains(m_connectionName)) return false;
    return QSqlDatabase::database(m_connectionName).isOpen();
}

bool DbManager::initTablesIfSqlite() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    // SQLite için temel tablolar
    q.exec("CREATE TABLE IF NOT EXISTS system_settings (setting_key TEXT PRIMARY KEY, setting_value TEXT, description TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS satellite_profiles (id INTEGER PRIMARY KEY AUTOINCREMENT, profile_name TEXT, satellite_name TEXT, orbital_position TEXT, default_tolerance_db REAL, is_active INTEGER);");
    q.exec("CREATE TABLE IF NOT EXISTS profile_channels (id INTEGER PRIMARY KEY AUTOINCREMENT, profile_id INTEGER, channel_no INTEGER, channel_name TEXT, frequency_mhz INTEGER, symbol_rate INTEGER, polarization TEXT, tolerance_db REAL, golden_cn_db REAL DEFAULT 0.0, golden_pow_dbm REAL DEFAULT 0.0, has_golden_sample INTEGER DEFAULT 0);");
    q.exec("CREATE TABLE IF NOT EXISTS work_orders (id INTEGER PRIMARY KEY AUTOINCREMENT, order_no TEXT, customer_name TEXT, product_code TEXT, satellite_profile_id INTEGER, target_quantity INTEGER, completed_quantity INTEGER, passed_quantity INTEGER, failed_quantity INTEGER, status TEXT, notes TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS test_runs (id INTEGER PRIMARY KEY AUTOINCREMENT, work_order_id INTEGER, station_id INTEGER, operator_id INTEGER, tester_name TEXT DEFAULT '', operator_username TEXT DEFAULT '', antenna_sn TEXT, satellite_profile_id INTEGER, test_timestamp TEXT, duration_sec INTEGER, overall_result TEXT, fail_reason TEXT, avg_delta_db REAL, max_delta_db REAL, notes TEXT);");
    q.exec("ALTER TABLE test_runs ADD COLUMN operator_username TEXT DEFAULT '';");
    q.exec("CREATE TABLE IF NOT EXISTS test_run_channels (id INTEGER PRIMARY KEY AUTOINCREMENT, test_run_id INTEGER, channel_no INTEGER, channel_name TEXT, frequency_mhz INTEGER, symbol_rate INTEGER, polarization TEXT, dut_cn_avg REAL, dut_pow_avg REAL, ref_cn_avg REAL, ref_pow_avg REAL, delta_db REAL, carrier_locked INTEGER, video_locked INTEGER, audio_locked INTEGER, tolerance_db REAL, result TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS golden_frequencies (id INTEGER PRIMARY KEY AUTOINCREMENT, label TEXT DEFAULT '', frequency_mhz INTEGER NOT NULL, symbol_rate INTEGER NOT NULL, polarization TEXT NOT NULL, golden_cn_db REAL DEFAULT 0.0, golden_pow_dbm REAL DEFAULT 0.0, tolerance_db REAL DEFAULT 1.50, updated_at TEXT DEFAULT CURRENT_TIMESTAMP, UNIQUE(frequency_mhz, symbol_rate, polarization));");

    // Varsayılan profil var mı kontrol et
    q.exec("SELECT COUNT(*) FROM satellite_profiles;");
    if (q.next() && q.value(0).toInt() == 0) {
        q.exec("INSERT INTO satellite_profiles (id, profile_name, satellite_name, orbital_position, default_tolerance_db, is_active) VALUES (1, 'Türksat Standart (10 Kanal)', 'Türksat 42.0E', '42.0E', 1.50, 1);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 1, 'TRT 1 HD', 11054, 30000, 'V', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 2, 'TRT SPOR HD', 11958, 27500, 'V', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 3, 'ATV HD', 12053, 27500, 'H', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 4, 'KANAL D HD', 12245, 27500, 'H', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 5, 'STAR TV HD', 12015, 27500, 'H', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 6, 'SHOW TV HD', 12210, 10000, 'H', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 7, 'A HABER HD', 12053, 27500, 'H', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 8, 'CNN TURK HD', 12245, 27500, 'H', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 9, 'HABERTURK HD', 12209, 10000, 'H', 1.50);");
        q.exec("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db) VALUES (1, 10, 'NTV HD', 12015, 27500, 'H', 1.50);");

        q.exec("INSERT INTO work_orders (id, order_no, customer_name, product_code, satellite_profile_id, target_quantity, status) VALUES (1, 'NETA-2026-001', 'Genel Üretim', 'DRX9000-ANT-V1', 1, 100, 'IN_PROGRESS');");
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
        qWarning() << "[DbManager] SQL betik dosyası açılamadı:" << resourceOrFilePath;
        return false;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");
    QString content = in.readAll();
    file.close();

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    QString currentQuery;
    const QStringList lines = content.split('\n');
    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith("--") || line.startsWith("/*")) {
            continue;
        }

        if (line.startsWith("USE ", Qt::CaseInsensitive)) {
            continue;
        }

        currentQuery += line + " ";
        if (line.endsWith(";")) {
            currentQuery.chop(1);
            currentQuery = currentQuery.trimmed();
            if (!currentQuery.isEmpty()) {
                if (!q.exec(currentQuery)) {
                    qWarning() << "[DbManager] SQL yürütme uyarısı:" << q.lastError().text()
                               << "\nSorgu:" << currentQuery.left(120);
                }
            }
            currentQuery.clear();
        }
    }

    if (!currentQuery.trimmed().isEmpty()) {
        q.exec(currentQuery.trimmed());
    }

    return true;
}

bool DbManager::ensureDefaultDataIfMissing() {
    if (!isConnected()) {
        qWarning() << "[DbManager] ensureDefaultDataIfMissing çağrıldı ancak veritabanı bağlı değil.";
        return false;
    }

    if (m_driverName == "QSQLITE") {
        return initTablesIfSqlite();
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 1. Tabloların varlığını denetle (satellite_profiles, profile_channels, golden_frequencies)
    bool needSchema = false;
    for (const char *tbl : {"satellite_profiles", "profile_channels", "golden_frequencies"}) {
        q.exec(QString("SHOW TABLES LIKE '%1';").arg(tbl));
        if (!q.next() || q.value(0).toString().isEmpty()) {
            needSchema = true;
            break;
        }
    }

    if (needSchema) {
        qInfo() << "[DbManager] MySQL tabloları bulunamadı, şema otomatik yükleniyor...";
        executeSqlScript(":/sql/schema_mysql.sql");

        // Emniyet ağı: profile_channels veya golden_frequencies hala yoksa doğrudan SQL ile oluştur
        q.exec("SHOW TABLES LIKE 'profile_channels';");
        if (!q.next() || q.value(0).toString().isEmpty()) {
            q.exec("CREATE TABLE IF NOT EXISTS `profile_channels` ("
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
        q.exec("SHOW TABLES LIKE 'golden_frequencies';");
        if (!q.next() || q.value(0).toString().isEmpty()) {
            q.exec("CREATE TABLE IF NOT EXISTS `golden_frequencies` ("
                   "`id` INT AUTO_INCREMENT PRIMARY KEY,"
                   "`label` VARCHAR(100) NOT NULL DEFAULT '',"
                   "`frequency_mhz` INT NOT NULL,"
                   "`symbol_rate` INT NOT NULL,"
                   "`polarization` ENUM('V', 'H') NOT NULL,"
                   "`golden_cn_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,"
                   "`golden_pow_dbm` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,"
                   "`tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,"
                   "`updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                   "UNIQUE KEY `uk_golden_freq_key` (`frequency_mhz`, `symbol_rate`, `polarization`)"
                   ") ENGINE=InnoDB;");
        }
    }

    // 2. Varsayılan profil ve kanalların varlığını denetle
    bool needSeed = false;
    q.exec("SELECT COUNT(*) FROM satellite_profiles;");
    if (!q.next() || q.value(0).toInt() == 0) {
        needSeed = true;
    } else {
        q.exec("SELECT COUNT(*) FROM profile_channels;");
        if (!q.next() || q.value(0).toInt() == 0) {
            needSeed = true;
        }
    }

    if (needSeed) {
        qInfo() << "[DbManager] Varsayılan test profilleri ve kanalları bulunamadı, seed verisi yükleniyor...";
        executeSqlScript(":/sql/seed_data.sql");
    }

    // 3. Sistem ayarlarını doğrula (yoksa varsayılanları ekle)
    q.exec("SELECT COUNT(*) FROM system_settings;");
    if (!q.next() || q.value(0).toInt() == 0) {
        q.exec("INSERT INTO system_settings (setting_key, setting_value, description) "
               "VALUES ('LOCK_TIMEOUT_SEC', '90', 'Referans cihazı kilit emniyet zaman aşımı süresi (saniye)') "
               "ON DUPLICATE KEY UPDATE setting_key=setting_key;");
        q.exec("INSERT INTO system_settings (setting_key, setting_value, description) "
               "VALUES ('SAMPLE_DURATION_SEC', '5', 'Kanal başına ortalama alma süresi (saniye)') "
               "ON DUPLICATE KEY UPDATE setting_key=setting_key;");
        q.exec("INSERT INTO system_settings (setting_key, setting_value, description) "
               "VALUES ('DEFAULT_TOLERANCE_DB', '1.50', 'Varsayılan Delta dB izin verilen tolerans payı') "
               "ON DUPLICATE KEY UPDATE setting_key=setting_key;");
        q.exec("INSERT INTO system_settings (setting_key, setting_value, description) "
               "VALUES ('TCP_PORT', '9100', 'Merkezi Host TCP komut ve senkronizasyon portu') "
               "ON DUPLICATE KEY UPDATE setting_key=setting_key;");
        q.exec("INSERT INTO system_settings (setting_key, setting_value, description) "
               "VALUES ('UDP_BEACON_PORT', '9101', 'LAN lobi anonsu ve otomatik keşif portu') "
               "ON DUPLICATE KEY UPDATE setting_key=setting_key;");
    }

    // 4. Varsayılan iş emri var mı?
    q.exec("SELECT COUNT(*) FROM work_orders;");
    if (!q.next() || q.value(0).toInt() == 0) {
        q.exec("INSERT INTO work_orders (id, order_no, customer_name, product_code, satellite_profile_id, target_quantity, status, notes) "
               "VALUES (1, 'NETA-2026-001', 'Genel Üretim', 'DRX9000-ANT-V1', 1, 100, 'IN_PROGRESS', 'Fabrika seri üretim parti 1') "
               "ON DUPLICATE KEY UPDATE order_no=order_no;");
    }

    // 5. Algoritma 2 Dağıtık Referans Kilit Tablosu
    q.exec("CREATE TABLE IF NOT EXISTS reference_lock ("
           "id INT PRIMARY KEY DEFAULT 1, "
           "locked_by_station INT NOT NULL DEFAULT 0, "
           "current_channel_no INT NOT NULL DEFAULT 0, "
           "lock_acquired_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, "
           "heartbeat_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP) ENGINE=InnoDB;");
    q.exec("INSERT INTO reference_lock (id, locked_by_station, current_channel_no) VALUES (1, 0, 0) "
           "ON DUPLICATE KEY UPDATE id=id;");

    // 6. Geriye uyumlu şema göçü: profile_channels tablosunda Golden Sample sütunları yoksa ekle
    q.exec("SHOW COLUMNS FROM profile_channels LIKE 'golden_cn_db';");
    if (!q.next()) {
        q.exec("ALTER TABLE profile_channels "
               "ADD COLUMN golden_cn_db DECIMAL(5, 2) NOT NULL DEFAULT 0.00 AFTER tolerance_db, "
               "ADD COLUMN golden_pow_dbm DECIMAL(6, 2) NOT NULL DEFAULT 0.00 AFTER golden_cn_db, "
               "ADD COLUMN has_golden_sample TINYINT(1) NOT NULL DEFAULT 0 AFTER golden_pow_dbm, "
               "ADD COLUMN golden_updated_at TIMESTAMP NULL DEFAULT NULL AFTER has_golden_sample;");
    }

    // 6b. Geriye uyumlu şema göçü: test_runs tablosunda tester_name / operator_username sütunları yoksa ekle
    q.exec("SHOW COLUMNS FROM test_runs LIKE 'tester_name';");
    if (!q.next()) {
        q.exec("ALTER TABLE test_runs ADD COLUMN tester_name VARCHAR(100) NOT NULL DEFAULT '' AFTER operator_id;");
    }
    q.exec("SHOW COLUMNS FROM test_runs LIKE 'operator_username';");
    if (!q.next()) {
        q.exec("ALTER TABLE test_runs ADD COLUMN operator_username VARCHAR(64) NOT NULL DEFAULT '' AFTER tester_name;");
    }

    // 6c. Yönetici frekans bazlı Golden Sample havuzu tablosu
    q.exec("CREATE TABLE IF NOT EXISTS golden_frequencies ("
           "id INT AUTO_INCREMENT PRIMARY KEY, "
           "label VARCHAR(64) NOT NULL DEFAULT '', "
           "frequency_mhz INT NOT NULL, "
           "symbol_rate INT NOT NULL, "
           "polarization ENUM('V', 'H') NOT NULL, "
           "golden_cn_db DECIMAL(5, 2) NOT NULL DEFAULT 0.00, "
           "golden_pow_dbm DECIMAL(6, 2) NOT NULL DEFAULT 0.00, "
           "tolerance_db DECIMAL(4, 2) NOT NULL DEFAULT 1.50, "
           "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, "
           "UNIQUE KEY uk_golden_freq (frequency_mhz, symbol_rate, polarization)) ENGINE=InnoDB;");

    // Referans Modu varsayılanı yoksa ekle
    q.exec("INSERT INTO system_settings (setting_key, setting_value, description) "
           "VALUES ('REFERENCE_MODE', 'LIVE', 'Referans kaynağı: LIVE (Canlı Anten) veya GOLDEN (Kayıtlı Golden Sample)') "
           "ON DUPLICATE KEY UPDATE setting_key=setting_key;");

    // 7. Önceki latin1/mojibake oturumlardan DB'de bozuk kalmış Türkçe seed metinlerini düzelt
    q.exec("SET NAMES 'utf8mb4'");
    q.exec("UPDATE satellite_profiles "
           "SET profile_name = 'Türksat Standart Üretim Testi (10 Kanal)', "
           "    satellite_name = 'Türksat 42.0°E' "
           "WHERE id = 1 AND (profile_name LIKE '%Ã%' OR satellite_name LIKE '%Ã%' OR satellite_name LIKE '%Â%');");
    q.exec("UPDATE work_orders "
           "SET customer_name = 'Genel Üretim' "
           "WHERE id = 1 AND customer_name LIKE '%Ã%';");
    q.exec("UPDATE users SET full_name = 'Sistem Yöneticisi' WHERE username = 'admin' AND full_name LIKE '%Ã%';");
    q.exec("UPDATE users SET full_name = 'Masa 1 Operatörü' WHERE username = 'operator1' AND full_name LIKE '%Ã%';");
    q.exec("UPDATE users SET full_name = 'Masa 2 Operatörü' WHERE username = 'operator2' AND full_name LIKE '%Ã%';");
    q.exec("UPDATE users SET full_name = 'Masa 3 Operatörü' WHERE username = 'operator3' AND full_name LIKE '%Ã%';");
    q.exec("UPDATE users SET password_hash = '123' WHERE password_hash IN ('neta123', 'op123', 'admin', '1234', 'tech');");

    qInfo() << "[MySQL Servisi] Veritabanı şeması ve fabrika başlangıç değerleri hazır.";
    return true;
}

QVector<SatelliteProfileInfo> DbManager::getProfiles() {
    QVector<SatelliteProfileInfo> list;
    if (!isConnected()) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, profile_name, satellite_name, orbital_position, default_tolerance_db, is_active FROM satellite_profiles WHERE is_active = 1 ORDER BY id ASC", db);

    while (q.next()) {
        SatelliteProfileInfo p;
        p.id = q.value(0).toInt();
        p.profileName = q.value(1).toString();
        p.satelliteName = q.value(2).toString();
        p.orbitalPosition = q.value(3).toString();
        p.defaultToleranceDb = q.value(4).toDouble();
        p.isActive = q.value(5).toBool();

        // Kanalları ve Golden Sample verilerini doldur
        QSqlQuery qCh(db);
        qCh.prepare("SELECT channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db, "
                    "golden_cn_db, golden_pow_dbm, has_golden_sample "
                    "FROM profile_channels WHERE profile_id = :pid ORDER BY channel_no ASC");
        qCh.bindValue(":pid", p.id);
        if (qCh.exec()) {
            while (qCh.next()) {
                ChannelConfig c;
                c.channelNo = qCh.value(0).toInt();
                c.channelName = qCh.value(1).toString();
                c.frequencyMhz = qCh.value(2).toInt();
                c.symbolRate = qCh.value(3).toInt();
                c.polarization = qCh.value(4).toString();
                c.toleranceDb = qCh.value(5).toDouble();
                c.goldenCnDb = qCh.value(6).toDouble();
                c.goldenPowDbm = qCh.value(7).toDouble();
                c.hasGoldenSample = qCh.value(8).toBool();
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
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    db.transaction();

    int profileId = profile.id;
    QSqlQuery q(db);

    if (profileId <= 0) {
        q.prepare("INSERT INTO satellite_profiles (profile_name, satellite_name, orbital_position, default_tolerance_db, is_active) "
                  "VALUES (:pname, :sname, :orb, :tol, :act)");
        q.bindValue(":pname", profile.profileName);
        q.bindValue(":sname", profile.satelliteName);
        q.bindValue(":orb", profile.orbitalPosition);
        q.bindValue(":tol", profile.defaultToleranceDb);
        q.bindValue(":act", profile.isActive ? 1 : 0);
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            db.rollback();
            return false;
        }
        profileId = q.lastInsertId().toInt();
    } else {
        q.prepare("UPDATE satellite_profiles SET profile_name = :pname, satellite_name = :sname, "
                  "orbital_position = :orb, default_tolerance_db = :tol, is_active = :act WHERE id = :id");
        q.bindValue(":pname", profile.profileName);
        q.bindValue(":sname", profile.satelliteName);
        q.bindValue(":orb", profile.orbitalPosition);
        q.bindValue(":tol", profile.defaultToleranceDb);
        q.bindValue(":act", profile.isActive ? 1 : 0);
        q.bindValue(":id", profileId);
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            db.rollback();
            return false;
        }
    }

    // Mevcut kanalları temizle ve güncel listeyi ekle
    QSqlQuery qDel(db);
    qDel.prepare("DELETE FROM profile_channels WHERE profile_id = :pid");
    qDel.bindValue(":pid", profileId);
    if (!qDel.exec()) {
        if (qDel.lastError().text().contains("doesn't exist", Qt::CaseInsensitive)) {
            db.rollback();
            ensureDefaultDataIfMissing();
            db.transaction();
            qDel.prepare("DELETE FROM profile_channels WHERE profile_id = :pid");
            qDel.bindValue(":pid", profileId);
            if (!qDel.exec()) {
                m_lastError = qDel.lastError().text();
                db.rollback();
                return false;
            }
        } else {
            m_lastError = qDel.lastError().text();
            db.rollback();
            return false;
        }
    }

    for (const auto &ch : profile.channels) {
        QSqlQuery qIns(db);
        qIns.prepare("INSERT INTO profile_channels (profile_id, channel_no, channel_name, frequency_mhz, "
                     "symbol_rate, polarization, tolerance_db, golden_cn_db, golden_pow_dbm, has_golden_sample) "
                     "VALUES (:pid, :cno, :cname, :freq, :sr, :pol, :tol, :gcn, :gpow, :hasg)");
        qIns.bindValue(":pid", profileId);
        qIns.bindValue(":cno", ch.channelNo);
        qIns.bindValue(":cname", ch.channelName);
        qIns.bindValue(":freq", ch.frequencyMhz);
        qIns.bindValue(":sr", ch.symbolRate);
        qIns.bindValue(":pol", ch.polarization);
        qIns.bindValue(":tol", ch.toleranceDb);
        qIns.bindValue(":gcn", ch.goldenCnDb);
        qIns.bindValue(":gpow", ch.goldenPowDbm);
        qIns.bindValue(":hasg", ch.hasGoldenSample ? 1 : 0);
        if (!qIns.exec()) {
            m_lastError = qIns.lastError().text();
            db.rollback();
            return false;
        }
    }

    return db.commit();
}

bool DbManager::deleteProfile(int profileId) {
    if (!isConnected() || profileId <= 1) return false; // Varsayılan profil 1 silinmez
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("UPDATE satellite_profiles SET is_active = 0 WHERE id = :id");
    q.bindValue(":id", profileId);
    return q.exec();
}

bool DbManager::updateChannelGoldenSample(int profileId, int channelNo, double cnDb, double powDbm) {
    if (!isConnected() || profileId <= 0) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (m_driverName == "QSQLITE") {
        q.prepare("UPDATE profile_channels SET golden_cn_db = :cn, golden_pow_dbm = :pow, has_golden_sample = 1 "
                  "WHERE profile_id = :pid AND channel_no = :cno");
    } else {
        q.prepare("UPDATE profile_channels SET golden_cn_db = :cn, golden_pow_dbm = :pow, has_golden_sample = 1, "
                  "golden_updated_at = CURRENT_TIMESTAMP WHERE profile_id = :pid AND channel_no = :cno");
    }
    q.bindValue(":cn", cnDb);
    q.bindValue(":pow", powDbm);
    q.bindValue(":pid", profileId);
    q.bindValue(":cno", channelNo);
    return q.exec();
}

ReferenceMode DbManager::getReferenceMode() {
    QString val = getSetting("REFERENCE_MODE", "LIVE");
    return (val.compare("GOLDEN", Qt::CaseInsensitive) == 0)
               ? ReferenceMode::GoldenSample
               : ReferenceMode::LiveReference;
}

bool DbManager::setReferenceMode(ReferenceMode mode) {
    QString val = (mode == ReferenceMode::GoldenSample) ? "GOLDEN" : "LIVE";
    return setSetting("REFERENCE_MODE", val,
                      "Referans kaynağı: LIVE (Canlı Anten) veya GOLDEN (Kayıtlı Golden Sample)");
}

QVector<GoldenFrequencyRecord> DbManager::getGoldenFrequencies() {
    QVector<GoldenFrequencyRecord> list;
    if (!isConnected()) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, label, frequency_mhz, symbol_rate, polarization, "
                "golden_cn_db, golden_pow_dbm, tolerance_db, updated_at "
                "FROM golden_frequencies ORDER BY frequency_mhz ASC, polarization ASC", db);
    while (q.next()) {
        GoldenFrequencyRecord r;
        r.id = q.value(0).toInt();
        r.label = q.value(1).toString();
        r.frequencyMhz = q.value(2).toInt();
        r.symbolRate = q.value(3).toInt();
        r.polarization = q.value(4).toString();
        r.goldenCnDb = q.value(5).toDouble();
        r.goldenPowDbm = q.value(6).toDouble();
        r.toleranceDb = q.value(7).toDouble();
        r.updatedAt = q.value(8).toString();
        list.append(r);
    }
    return list;
}

bool DbManager::saveGoldenFrequency(const GoldenFrequencyRecord &rec) {
    if (!isConnected()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (m_driverName == "QSQLITE") {
        if (rec.id > 0) {
            q.prepare("UPDATE golden_frequencies SET label = :lbl, frequency_mhz = :freq, symbol_rate = :sr, "
                      "polarization = :pol, golden_cn_db = :cn, golden_pow_dbm = :pow, tolerance_db = :tol, "
                      "updated_at = CURRENT_TIMESTAMP WHERE id = :id");
            q.bindValue(":id", rec.id);
        } else {
            q.prepare("INSERT OR REPLACE INTO golden_frequencies (label, frequency_mhz, symbol_rate, polarization, "
                      "golden_cn_db, golden_pow_dbm, tolerance_db, updated_at) "
                      "VALUES (:lbl, :freq, :sr, :pol, :cn, :pow, :tol, CURRENT_TIMESTAMP)");
        }
    } else if (rec.id > 0) {
        q.prepare("UPDATE golden_frequencies SET label = :lbl, frequency_mhz = :freq, symbol_rate = :sr, "
                  "polarization = :pol, golden_cn_db = :cn, golden_pow_dbm = :pow, tolerance_db = :tol "
                  "WHERE id = :id");
        q.bindValue(":id", rec.id);
    } else {
        q.prepare("INSERT INTO golden_frequencies (label, frequency_mhz, symbol_rate, polarization, "
                  "golden_cn_db, golden_pow_dbm, tolerance_db) "
                  "VALUES (:lbl, :freq, :sr, :pol, :cn, :pow, :tol) "
                  "ON DUPLICATE KEY UPDATE label = VALUES(label), golden_cn_db = VALUES(golden_cn_db), "
                  "golden_pow_dbm = VALUES(golden_pow_dbm), tolerance_db = VALUES(tolerance_db)");
    }

    q.bindValue(":lbl", rec.label);
    q.bindValue(":freq", rec.frequencyMhz);
    q.bindValue(":sr", rec.symbolRate);
    q.bindValue(":pol", rec.polarization);
    q.bindValue(":cn", rec.goldenCnDb);
    q.bindValue(":pow", rec.goldenPowDbm);
    q.bindValue(":tol", rec.toleranceDb);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DbManager::deleteGoldenFrequency(int id) {
    if (!isConnected() || id <= 0) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM golden_frequencies WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}

bool DbManager::findGoldenSampleForChannel(int freqMhz, int symbolRate, const QString &pol,
                                           double &outCn, double &outPow, double &outTol) {
    if (!isConnected()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 1. Öncelik: Tam frekans + sembol hızı + polarizasyon eşleşmesi
    q.prepare("SELECT golden_cn_db, golden_pow_dbm, tolerance_db FROM golden_frequencies "
              "WHERE frequency_mhz = :f AND symbol_rate = :sr AND polarization = :pol LIMIT 1");
    q.bindValue(":f", freqMhz);
    q.bindValue(":sr", symbolRate);
    q.bindValue(":pol", pol);
    if (q.exec() && q.next()) {
        outCn = q.value(0).toDouble();
        outPow = q.value(1).toDouble();
        outTol = q.value(2).toDouble();
        return (outCn > 0.1 || qAbs(outPow) > 0.1);
    }

    // 2. İkincil: Frekans + polarizasyon eşleşmesi
    q.prepare("SELECT golden_cn_db, golden_pow_dbm, tolerance_db FROM golden_frequencies "
              "WHERE frequency_mhz = :f AND polarization = :pol ORDER BY id DESC LIMIT 1");
    q.bindValue(":f", freqMhz);
    q.bindValue(":pol", pol);
    if (q.exec() && q.next()) {
        outCn = q.value(0).toDouble();
        outPow = q.value(1).toDouble();
        outTol = q.value(2).toDouble();
        return (outCn > 0.1 || qAbs(outPow) > 0.1);
    }

    return false;
}

QVector<WorkOrderInfo> DbManager::getActiveWorkOrders() {
    QVector<WorkOrderInfo> list;
    if (!isConnected()) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, order_no, customer_name, product_code, satellite_profile_id, target_quantity, "
                "completed_quantity, passed_quantity, failed_quantity, status, notes "
                "FROM work_orders WHERE status != 'COMPLETED' AND status != 'CANCELLED' ORDER BY id DESC", db);

    while (q.next()) {
        WorkOrderInfo o;
        o.id = q.value(0).toInt();
        o.orderNo = q.value(1).toString();
        o.customerName = q.value(2).toString();
        o.productCode = q.value(3).toString();
        o.satelliteProfileId = q.value(4).toInt();
        o.targetQuantity = q.value(5).toInt();
        o.completedQuantity = q.value(6).toInt();
        o.passedQuantity = q.value(7).toInt();
        o.failedQuantity = q.value(8).toInt();
        o.status = q.value(9).toString();
        o.notes = q.value(10).toString();
        list.append(o);
    }
    return list;
}

bool DbManager::incrementOrderCount(int workOrderId, bool isPass) {
    if (!isConnected()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (isPass) {
        q.prepare("UPDATE work_orders SET completed_quantity = completed_quantity + 1, "
                  "passed_quantity = passed_quantity + 1 WHERE id = :id");
    } else {
        q.prepare("UPDATE work_orders SET completed_quantity = completed_quantity + 1, "
                  "failed_quantity = failed_quantity + 1 WHERE id = :id");
    }
    q.bindValue(":id", workOrderId);
    return q.exec();
}

qint64 DbManager::insertTestRun(const TestRunData &run) {
    if (!isConnected()) return -1;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    db.transaction();

    int targetProfileId = run.satelliteProfileId;
    if (targetProfileId <= 0 && !run.profileName.trimmed().isEmpty()) {
        QSqlQuery qProf(db);
        qProf.prepare("SELECT id FROM satellite_profiles WHERE profile_name = :pname LIMIT 1");
        qProf.bindValue(":pname", run.profileName.trimmed());
        if (qProf.exec() && qProf.next()) {
            targetProfileId = qProf.value(0).toInt();
        }
    }
    if (targetProfileId <= 0) {
        QSqlQuery qMin("SELECT MIN(id) FROM satellite_profiles", db);
        if (qMin.next() && qMin.value(0).toInt() > 0) {
            targetProfileId = qMin.value(0).toInt();
        } else {
            targetProfileId = 1;
        }
    }

    const QString opUser = run.operatorUsername.trimmed().isEmpty()
                               ? AppConfig::instance().currentUsername()
                               : run.operatorUsername.trimmed();

    QSqlQuery q(db);
    q.prepare("INSERT INTO test_runs (work_order_id, station_id, operator_id, tester_name, operator_username, antenna_sn, "
              "satellite_profile_id, test_timestamp, duration_sec, overall_result, fail_reason, "
              "avg_delta_db, max_delta_db, notes) "
              "VALUES (:wo, :st, :op, :tname, :opuser, :sn, :sp, :ts, :dur, :res, :fr, :avg, :max, :notes)");
    q.bindValue(":wo", run.workOrderId > 0 ? run.workOrderId : QVariant());
    q.bindValue(":st", run.stationId);
    q.bindValue(":op", run.operatorId > 0 ? run.operatorId : QVariant());
    q.bindValue(":tname", run.testerName);
    q.bindValue(":opuser", opUser);
    q.bindValue(":sn", run.antennaSn);
    q.bindValue(":sp", targetProfileId);
    q.bindValue(":ts", (run.timestamp.isValid() ? run.timestamp : QDateTime::currentDateTime()).toString(Qt::ISODate));
    q.bindValue(":dur", run.durationSec);
    q.bindValue(":res", run.overallResult);
    q.bindValue(":fr", run.failReason);
    q.bindValue(":avg", run.avgDeltaDb);
    q.bindValue(":max", run.maxDeltaDb);
    q.bindValue(":notes", run.notes);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[DbManager] test_runs insert failed:" << m_lastError;
        db.rollback();
        return -1;
    }

    qint64 runId = q.lastInsertId().toLongLong();

    // 10 Kanalı Ekle
    for (const auto &ch : run.channels) {
        QSqlQuery qCh(db);
        qCh.prepare("INSERT INTO test_run_channels (test_run_id, channel_no, channel_name, frequency_mhz, "
                    "symbol_rate, polarization, dut_cn_avg, dut_pow_avg, ref_cn_avg, ref_pow_avg, delta_db, "
                    "carrier_locked, video_locked, audio_locked, tolerance_db, result) "
                    "VALUES (:tr_id, :ch_no, :ch_name, :freq, :sr, :pol, :dut_cn, :dut_pow, :ref_cn, :ref_pow, "
                    ":delta, :c_lock, :v_lock, :a_lock, :tol, :res)");
        qCh.bindValue(":tr_id", runId);
        qCh.bindValue(":ch_no", ch.channelNo);
        qCh.bindValue(":ch_name", ch.channelName);
        qCh.bindValue(":freq", ch.frequencyMhz);
        qCh.bindValue(":sr", ch.symbolRate);
        qCh.bindValue(":pol", ch.polarization);
        qCh.bindValue(":dut_cn", ch.dutCn);
        qCh.bindValue(":dut_pow", ch.dutPow);
        qCh.bindValue(":ref_cn", ch.refCn);
        qCh.bindValue(":ref_pow", ch.refPow);
        qCh.bindValue(":delta", ch.deltaDb);
        qCh.bindValue(":c_lock", ch.carrierLocked ? 1 : 0);
        qCh.bindValue(":v_lock", ch.videoLocked ? 1 : 0);
        qCh.bindValue(":a_lock", ch.audioLocked ? 1 : 0);
        qCh.bindValue(":tol", ch.toleranceDb);
        qCh.bindValue(":res", ch.status);

        if (!qCh.exec()) {
            qWarning() << "[DbManager] test_run_channels insert failed:" << qCh.lastError().text();
        }
    }

    db.commit();

    if (run.workOrderId > 0) {
        incrementOrderCount(run.workOrderId, run.overallResult == "PASS");
    }

    return runId;
}

QVector<TestRunData> DbManager::getTestRuns(int limit, const QString &searchSn) {
    QVector<TestRunData> list;
    if (!isConnected()) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QString sql = "SELECT tr.id, tr.work_order_id, tr.station_id, tr.operator_id, tr.tester_name, "
                  "COALESCE(NULLIF(tr.operator_username, ''), u.username, '') AS op_username, "
                  "tr.antenna_sn, tr.satellite_profile_id, COALESCE(sp.profile_name, '') AS prof_name, "
                  "tr.test_timestamp, tr.duration_sec, tr.overall_result, tr.fail_reason, "
                  "tr.avg_delta_db, tr.max_delta_db, tr.notes "
                  "FROM test_runs tr "
                  "LEFT JOIN users u ON u.id = tr.operator_id "
                  "LEFT JOIN satellite_profiles sp ON sp.id = tr.satellite_profile_id ";

    const QString needle = searchSn.trimmed();
    if (!needle.isEmpty()) {
        sql += "WHERE tr.antenna_sn LIKE :s1 OR tr.tester_name LIKE :s2 "
               "OR tr.operator_username LIKE :s3 OR u.username LIKE :s4 "
               "OR sp.profile_name LIKE :s5 OR tr.fail_reason LIKE :s6 OR tr.notes LIKE :s7 ";
    }
    sql += QString("ORDER BY tr.id DESC LIMIT %1").arg(qMax(1, limit));

    QSqlQuery q(db);
    q.prepare(sql);
    if (!needle.isEmpty()) {
        const QString like = QString("%%1%").arg(needle);
        q.bindValue(":s1", like);
        q.bindValue(":s2", like);
        q.bindValue(":s3", like);
        q.bindValue(":s4", like);
        q.bindValue(":s5", like);
        q.bindValue(":s6", like);
        q.bindValue(":s7", like);
    }

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[DbManager] getTestRuns failed:" << m_lastError;
        return list;
    }

    while (q.next()) {
        TestRunData r;
        r.id = q.value(0).toLongLong();
        r.workOrderId = q.value(1).toInt();
        r.stationId = q.value(2).toInt();
        r.operatorId = q.value(3).toInt();
        r.testerName = q.value(4).toString();
        r.operatorUsername = q.value(5).toString();
        r.antennaSn = q.value(6).toString();
        r.satelliteProfileId = q.value(7).toInt();
        r.profileName = q.value(8).toString();
        r.timestamp = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        if (!r.timestamp.isValid()) {
            r.timestamp = q.value(9).toDateTime();
        }
        r.durationSec = q.value(10).toInt();
        r.overallResult = q.value(11).toString();
        r.failReason = q.value(12).toString();
        r.avgDeltaDb = q.value(13).toDouble();
        r.maxDeltaDb = q.value(14).toDouble();
        r.notes = q.value(15).toString();
        list.append(r);
    }
    return list;
}

bool DbManager::testRunExists(const QString &antennaSn, int stationId, const QDateTime &timestamp) {
    if (!isConnected()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id FROM test_runs WHERE antenna_sn = :sn AND station_id = :st AND test_timestamp = :ts LIMIT 1");
    q.bindValue(":sn", antennaSn);
    q.bindValue(":st", stationId);
    q.bindValue(":ts", timestamp.toString(Qt::ISODate));
    return (q.exec() && q.next());
}

QString DbManager::getSetting(const QString &key, const QString &defaultValue) {
    if (!isConnected()) return defaultValue;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT setting_value FROM system_settings WHERE setting_key = :k");
    q.bindValue(":k", key);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return defaultValue;
}

bool DbManager::setSetting(const QString &key, const QString &value, const QString &desc) {
    if (!isConnected()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("INSERT INTO system_settings (setting_key, setting_value, description) "
              "VALUES (:k, :v, :d) ON DUPLICATE KEY UPDATE setting_value = :v, description = :d");
    q.bindValue(":k", key);
    q.bindValue(":v", value);
    q.bindValue(":d", desc);
    return q.exec();
}

bool DbManager::tryAcquireDbLock(int stationId, int &outLockedByStation, int &outCurrentChannel) {
    outLockedByStation = 0;
    outCurrentChannel = 0;
    if (!isConnected()) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // Atomic Mutex Acquisition:
    // If lock is vacant (0), already held by this station, or heartbeat expired (> 45s), claim lock!
    if (m_driverName == "QSQLITE") {
        q.prepare("UPDATE reference_lock SET locked_by_station = :sid WHERE id = 1 AND (locked_by_station = 0 OR locked_by_station = :sid)");
    } else {
        q.prepare("UPDATE reference_lock "
                  "SET locked_by_station = :sid, "
                  "    lock_acquired_at = CURRENT_TIMESTAMP, "
                  "    heartbeat_at = CURRENT_TIMESTAMP "
                  "WHERE id = 1 AND (locked_by_station = 0 OR locked_by_station = :sid "
                  "      OR TIMESTAMPDIFF(SECOND, heartbeat_at, NOW()) > 45)");
    }
    q.bindValue(":sid", stationId);

    if (q.exec() && q.numRowsAffected() > 0) {
        outLockedByStation = stationId;
        return true;
    }

    // Lock busy; identify active holding station and channel
    if (q.exec("SELECT locked_by_station, current_channel_no FROM reference_lock WHERE id = 1") && q.next()) {
        outLockedByStation = q.value(0).toInt();
        outCurrentChannel = q.value(1).toInt();
    }
    return false;
}

bool DbManager::releaseDbLock(int stationId) {
    if (!isConnected()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("UPDATE reference_lock SET locked_by_station = 0, current_channel_no = 0 "
              "WHERE id = 1 AND (locked_by_station = :sid OR :sid <= 0)");
    q.bindValue(":sid", stationId);
    return q.exec();
}

bool DbManager::updateDbLockHeartbeat(int stationId, int currentChannel) {
    if (!isConnected()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (m_driverName == "QSQLITE") {
        q.prepare("UPDATE reference_lock SET current_channel_no = :ch WHERE id = 1 AND locked_by_station = :sid");
    } else {
        q.prepare("UPDATE reference_lock SET heartbeat_at = CURRENT_TIMESTAMP, current_channel_no = :ch "
                  "WHERE id = 1 AND locked_by_station = :sid");
    }
    q.bindValue(":ch", currentChannel);
    q.bindValue(":sid", stationId);
    return q.exec();
}

int DbManager::getDbLockHolder(int &outCurrentChannel) {
    outCurrentChannel = 0;
    if (!isConnected()) return 0;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (m_driverName == "QSQLITE") {
        if (q.exec("SELECT locked_by_station, current_channel_no FROM reference_lock WHERE id = 1") && q.next()) {
            outCurrentChannel = q.value(1).toInt();
            return q.value(0).toInt();
        }
    } else {
        if (q.exec("SELECT locked_by_station, current_channel_no, TIMESTAMPDIFF(SECOND, heartbeat_at, NOW()) FROM reference_lock WHERE id = 1") && q.next()) {
            int holder = q.value(0).toInt();
            outCurrentChannel = q.value(1).toInt();
            int ageSec = q.value(2).toInt();
            if (ageSec > 45) {
                return 0; // Lock lease expired
            }
            return holder;
        }
    }
    return 0;
}

bool DbManager::startPortableMariaDbIfPresent() {
    if (QCoreApplication::instance()) {
        m_appDirPath = QCoreApplication::applicationDirPath();
    }
    QString appDir = m_appDirPath.isEmpty() ? QDir::currentPath() : m_appDirPath;
    QStringList candidates = {
        appDir + "/tools/mariadb_portable",
        appDir + "/mariadb_portable",
        QDir::currentPath() + "/tools/mariadb_portable",
        "D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/mariadb_portable"
    };

    QString portableDir;
    for (const auto &dir : candidates) {
        if (QFile::exists(dir + "/bin/mysqld.exe")) {
            portableDir = QDir::cleanPath(dir);
            break;
        }
    }

    if (portableDir.isEmpty()) {
        qWarning() << "[DbManager] Portable MariaDB directory not found.";
        return false;
    }

    qInfo() << "[DbManager] Portable MariaDB found at:" << portableDir;
    QString mysqldPath = portableDir + "/bin/mysqld.exe";
    QString iniPath = portableDir + "/my.ini";
    QString dataPath = portableDir + "/data";
    QString installDbPath = portableDir + "/bin/mysql_install_db.exe";

    if (!QDir(dataPath + "/mysql").exists() && QFile::exists(installDbPath)) {
        qInfo() << "[DbManager] Initializing clean portable data directory with mysql_install_db...";
        // Ensure data/ has no leftover partial files (mysql_install_db requires an empty datadir)
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
            d.mkpath(".");
        }

        QStringList initArgs;
        initArgs << QString("--datadir=%1").arg(dataPath)
                 << "--password=neta123"
                 << "--allow-remote-root-access"
                 << "--port=3306"
                 << "--silent";
        QProcess::execute(installDbPath, initArgs);
    }

    if (!m_portableProcess) {
        m_portableProcess = new QProcess(this);
    }

    if (m_portableProcess->state() == QProcess::NotRunning) {
        QStringList args;
        args << QString("--defaults-file=%1").arg(iniPath);
        args << QString("--datadir=%1").arg(dataPath);

        m_portableProcess->start(mysqldPath, args);
        if (!m_portableProcess->waitForStarted(3000)) {
            qWarning() << "[DbManager] Failed to launch portable MariaDB:" << m_portableProcess->errorString();
            return false;
        }

        m_launchedPortableMysqld = true;
        qInfo() << "[DbManager] Portable MariaDB launched in background (PID:" << m_portableProcess->processId() << ")";

        // Poll port 3306 up to 6s (first boot initializes InnoDB tablespaces)
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

void DbManager::stopPortableMariaDbIfRunning() {
    if (m_launchedPortableMysqld && m_portableProcess && m_portableProcess->state() != QProcess::NotRunning) {
        qInfo() << "[DbManager] Stopping local portable MariaDB process...";
        QString appDir = m_appDirPath.isEmpty() ? QDir::currentPath() : m_appDirPath;
        QStringList candidates = {
            appDir + "/tools/mariadb_portable/bin/mysqladmin.exe",
            appDir + "/mariadb_portable/bin/mysqladmin.exe",
            QDir::currentPath() + "/tools/mariadb_portable/bin/mysqladmin.exe",
            "D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/mariadb_portable/bin/mysqladmin.exe"
        };
        for (const auto &admin : candidates) {
            if (QFile::exists(admin)) {
                QProcess::execute(admin, QStringList() << "-h" << "127.0.0.1" << "-P" << "3306" << "-u" << "root" << "-pneta123" << "shutdown");
                break;
            }
        }
        if (!m_portableProcess->waitForFinished(4000)) {
            m_portableProcess->kill();
        }
        m_launchedPortableMysqld = false;
    }
}

bool DbManager::authenticateUser(const QString &username, const QString &password, UserRole &outRole, QString &outFullName) {
    QString trimmedUser = username.trimmed();
    QString trimmedPass = password.trimmed();

    if (trimmedUser.isEmpty()) {
        return false;
    }

    // 1. Veritabanı bağlıysa users tablosundan kontrol et
    if (isConnected()) {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        QSqlQuery q(db);
        q.prepare("SELECT role, full_name, password_hash FROM users WHERE username = :u AND is_active = 1 LIMIT 1;");
        q.bindValue(":u", trimmedUser);

        if (q.exec() && q.next()) {
            QString roleStr = q.value("role").toString().toUpper();
            QString fullName = q.value("full_name").toString();
            QString passHash = q.value("password_hash").toString();

            if (trimmedPass == passHash || passHash.isEmpty()) {
                outRole = (roleStr == "ADMIN") ? UserRole::Engineer : UserRole::Technician;
                outFullName = fullName.isEmpty() ? trimmedUser : fullName;
                return true;
            }
        }
    }

    // 2. Fabrika Varsayılan / Yedek Kimlik Doğrulama (Veritabanı kapalı olsa dahi çalışır)
    if (trimmedUser.compare("admin", Qt::CaseInsensitive) == 0 && trimmedPass == "123") {
        outRole = UserRole::Engineer;
        outFullName = "Sistem Yöneticisi / Başmühendis";
        return true;
    }
    if (trimmedUser.compare("engineer", Qt::CaseInsensitive) == 0 && trimmedPass == "123") {
        outRole = UserRole::Engineer;
        outFullName = "Ar-Ge Test Mühendisi";
        return true;
    }
    if (trimmedUser.compare("operator1", Qt::CaseInsensitive) == 0 && trimmedPass == "123") {
        outRole = UserRole::Technician;
        outFullName = "Masa 1 Operatörü";
        return true;
    }
    if (trimmedUser.compare("operator2", Qt::CaseInsensitive) == 0 && trimmedPass == "123") {
        outRole = UserRole::Technician;
        outFullName = "Masa 2 Operatörü";
        return true;
    }
    if (trimmedUser.compare("operator3", Qt::CaseInsensitive) == 0 && trimmedPass == "123") {
        outRole = UserRole::Technician;
        outFullName = "Masa 3 Operatörü";
        return true;
    }
    if ((trimmedUser.compare("tekniker", Qt::CaseInsensitive) == 0 || trimmedUser.compare("tech", Qt::CaseInsensitive) == 0) && trimmedPass == "123") {
        outRole = UserRole::Technician;
        outFullName = "Saha Test Teknikeri";
        return true;
    }

    return false;
}

QVector<UserRecord> DbManager::getUsers() {
    QVector<UserRecord> list;
    if (!isConnected()) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, username, password_hash, full_name, role, is_active FROM users ORDER BY id ASC;", db);
    while (q.next()) {
        UserRecord u;
        u.id = q.value(0).toInt();
        u.username = q.value(1).toString();
        u.password = q.value(2).toString();
        u.fullName = q.value(3).toString();
        u.role = q.value(4).toString().trimmed().toUpper();
        u.isActive = q.value(5).toInt() != 0;
        list.append(u);
    }
    return list;
}

bool DbManager::saveUser(const UserRecord &user) {
    if (!isConnected()) {
        if (m_lastError.isEmpty()) {
            m_lastError = "Veritabanı bağlantısı aktif değil.";
        }
        return false;
    }

    const QString uname = user.username.trimmed();
    if (uname.isEmpty()) {
        m_lastError = "Kullanıcı adı boş olamaz.";
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (user.id <= 0) {
        q.prepare("INSERT INTO users (username, password_hash, full_name, role, is_active) "
                  "VALUES (:u, :p, :fn, :r, :act);");
        q.bindValue(":u", uname);
        q.bindValue(":p", user.password.isEmpty() ? QString("123") : user.password);
        q.bindValue(":fn", user.fullName.trimmed().isEmpty() ? uname : user.fullName.trimmed());
        q.bindValue(":r", user.role.trimmed().toUpper() == "ADMIN" ? QString("ADMIN") : QString("OPERATOR"));
        q.bindValue(":act", user.isActive ? 1 : 0);
    } else if (!user.password.isEmpty()) {
        q.prepare("UPDATE users SET username = :u, password_hash = :p, full_name = :fn, role = :r, is_active = :act WHERE id = :id;");
        q.bindValue(":u", uname);
        q.bindValue(":p", user.password);
        q.bindValue(":fn", user.fullName.trimmed().isEmpty() ? uname : user.fullName.trimmed());
        q.bindValue(":r", user.role.trimmed().toUpper() == "ADMIN" ? QString("ADMIN") : QString("OPERATOR"));
        q.bindValue(":act", user.isActive ? 1 : 0);
        q.bindValue(":id", user.id);
    } else {
        q.prepare("UPDATE users SET username = :u, full_name = :fn, role = :r, is_active = :act WHERE id = :id;");
        q.bindValue(":u", uname);
        q.bindValue(":fn", user.fullName.trimmed().isEmpty() ? uname : user.fullName.trimmed());
        q.bindValue(":r", user.role.trimmed().toUpper() == "ADMIN" ? QString("ADMIN") : QString("OPERATOR"));
        q.bindValue(":act", user.isActive ? 1 : 0);
        q.bindValue(":id", user.id);
    }

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DbManager::deleteUser(int userId) {
    if (!isConnected() || userId <= 0) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery qCheck(db);
    qCheck.prepare("SELECT username FROM users WHERE id = :id;");
    qCheck.bindValue(":id", userId);
    if (qCheck.exec() && qCheck.next()) {
        if (qCheck.value(0).toString().compare("admin", Qt::CaseInsensitive) == 0) {
            m_lastError = "Ana 'admin' hesabı silinemez.";
            return false;
        }
    }

    QSqlQuery q(db);
    q.prepare("DELETE FROM users WHERE id = :id;");
    q.bindValue(":id", userId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

