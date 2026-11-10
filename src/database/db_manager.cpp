/**
 * @file db_manager.cpp
 * @brief Implementation of MariaDB/MySQL database management and distributed locking.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "db_manager.h"
#include <QDebug>
#include <QVariant>
#include <QSqlRecord>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStringList>
#include <QCoreApplication>

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
            qWarning() << "[Database Service] Unable to open database:" << m_lastError;
            emit connectionStatusChanged(false, m_lastError);
            return false;
        }
    }

    qDebug() << QString("[Database Service] Connected successfully to %1:%2 (Driver: %3)").arg(host).arg(port).arg(m_driverName);
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
    q.exec("CREATE TABLE IF NOT EXISTS profile_channels (id INTEGER PRIMARY KEY AUTOINCREMENT, profile_id INTEGER, channel_no INTEGER, channel_name TEXT, frequency_mhz INTEGER, symbol_rate INTEGER, polarization TEXT, tolerance_db REAL);");
    q.exec("CREATE TABLE IF NOT EXISTS work_orders (id INTEGER PRIMARY KEY AUTOINCREMENT, order_no TEXT, customer_name TEXT, product_code TEXT, satellite_profile_id INTEGER, target_quantity INTEGER, completed_quantity INTEGER, passed_quantity INTEGER, failed_quantity INTEGER, status TEXT, notes TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS test_runs (id INTEGER PRIMARY KEY AUTOINCREMENT, work_order_id INTEGER, station_id INTEGER, operator_id INTEGER, antenna_sn TEXT, satellite_profile_id INTEGER, test_timestamp TEXT, duration_sec INTEGER, overall_result TEXT, fail_reason TEXT, avg_delta_db REAL, max_delta_db REAL, notes TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS test_run_channels (id INTEGER PRIMARY KEY AUTOINCREMENT, test_run_id INTEGER, channel_no INTEGER, channel_name TEXT, frequency_mhz INTEGER, symbol_rate INTEGER, polarization TEXT, dut_cn_avg REAL, dut_pow_avg REAL, ref_cn_avg REAL, ref_pow_avg REAL, delta_db REAL, carrier_locked INTEGER, video_locked INTEGER, audio_locked INTEGER, tolerance_db REAL, result TEXT);");

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

    // 1. Tabloların varlığını denetle
    bool needSchema = false;
    q.exec("SHOW TABLES LIKE 'satellite_profiles';");
    if (!q.next() || q.value(0).toString().isEmpty()) {
        needSchema = true;
    }

    if (needSchema) {
        qInfo() << "[DbManager] MySQL tabloları bulunamadı, şema otomatik yükleniyor...";
        executeSqlScript(":/sql/schema_mysql.sql");
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

    qInfo() << "[MySQL Servisi] Veritabanı şeması ve fabrika başlangıç değerleri hazır.";
    return true;
}

QVector<SatelliteProfileInfo> DbManager::getProfiles() {
    QVector<SatelliteProfileInfo> list;
    if (!isConnected()) return list;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, profile_name, satellite_name, orbital_position, default_tolerance_db, is_active FROM satellite_profiles WHERE is_active = 1", db);

    while (q.next()) {
        SatelliteProfileInfo p;
        p.id = q.value(0).toInt();
        p.profileName = q.value(1).toString();
        p.satelliteName = q.value(2).toString();
        p.orbitalPosition = q.value(3).toString();
        p.defaultToleranceDb = q.value(4).toDouble();
        p.isActive = q.value(5).toBool();

        // Kanalları doldur
        QSqlQuery qCh(db);
        qCh.prepare("SELECT channel_no, channel_name, frequency_mhz, symbol_rate, polarization, tolerance_db "
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

    QSqlQuery q(db);
    q.prepare("INSERT INTO test_runs (work_order_id, station_id, operator_id, antenna_sn, "
              "satellite_profile_id, test_timestamp, duration_sec, overall_result, fail_reason, "
              "avg_delta_db, max_delta_db, notes) "
              "VALUES (:wo, :st, :op, :sn, :sp, :ts, :dur, :res, :fr, :avg, :max, :notes)");
    q.bindValue(":wo", run.workOrderId > 0 ? run.workOrderId : QVariant());
    q.bindValue(":st", run.stationId);
    q.bindValue(":op", run.operatorId);
    q.bindValue(":sn", run.antennaSn);
    q.bindValue(":sp", run.satelliteProfileId);
    q.bindValue(":ts", run.timestamp.toString(Qt::ISODate));
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
    QString sql = "SELECT id, work_order_id, station_id, operator_id, antenna_sn, satellite_profile_id, "
                  "test_timestamp, duration_sec, overall_result, fail_reason, avg_delta_db, max_delta_db, notes "
                  "FROM test_runs ";
    if (!searchSn.isEmpty()) {
        sql += QString("WHERE antenna_sn LIKE '%%1%' ").arg(searchSn);
    }
    sql += QString("ORDER BY id DESC LIMIT %1").arg(limit);

    QSqlQuery q(sql, db);
    while (q.next()) {
        TestRunData r;
        r.id = q.value(0).toLongLong();
        r.workOrderId = q.value(1).toInt();
        r.stationId = q.value(2).toInt();
        r.operatorId = q.value(3).toInt();
        r.antennaSn = q.value(4).toString();
        r.satelliteProfileId = q.value(5).toInt();
        r.timestamp = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        r.durationSec = q.value(7).toInt();
        r.overallResult = q.value(8).toString();
        r.failReason = q.value(9).toString();
        r.avgDeltaDb = q.value(10).toDouble();
        r.maxDeltaDb = q.value(11).toDouble();
        r.notes = q.value(12).toString();
        list.append(r);
    }
    return list;
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
        QThread::msleep(2000);
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

