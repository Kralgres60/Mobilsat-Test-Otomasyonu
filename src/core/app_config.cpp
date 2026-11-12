/**
 * @file app_config.cpp
 * @brief Implementation of AppConfig persistent portable INI settings loader and immediate saver.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "app_config.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <memory>

AppConfig& AppConfig::instance() {
    static AppConfig s_instance;
    return s_instance;
}

AppConfig::AppConfig() {
    load();
}

QString AppConfig::configFilePath() {
    QString baseDir = QCoreApplication::instance()
                          ? QCoreApplication::applicationDirPath()
                          : QDir::currentPath();
    return QDir(baseDir).filePath("neta_config.ini");
}

void AppConfig::load() {
    m_isLoading = true;
    const QString iniPath = configFilePath();
    const bool iniExists = QFile::exists(iniPath);

    QSettings settings(iniPath, QSettings::IniFormat);
    settings.setIniCodec("UTF-8");

    // If portable neta_config.ini does not exist yet, read any legacy registry values once as defaults
    std::unique_ptr<QSettings> legacySettings;
    if (!iniExists) {
        legacySettings = std::make_unique<QSettings>("NETA", "DRX_Antenna_Test");
    }
    auto readVal = [&](const QString &key, const QVariant &def) -> QVariant {
        if (settings.contains(key)) {
            return settings.value(key, def);
        }
        return (iniExists || !legacySettings) ? def : legacySettings->value(key, def);
    };

    const QString localHost = (settings.contains("General/StationAlias") && settings.contains("General/ServerName"))
                                  ? QString()
                                  : QHostInfo::localHostName();

    int roleInt = readVal("General/Role", static_cast<int>(AppRole::Client)).toInt();
    m_role = static_cast<AppRole>(roleInt);
    m_stationId = readVal("General/StationId", 1).toInt();
    m_stationAlias = readVal("General/StationAlias", localHost).toString();
    m_serverName = readVal("General/ServerName", QString("NETA-HOST (%1)").arg(localHost)).toString();

    int userRoleInt = readVal("User/Role", static_cast<int>(UserRole::Technician)).toInt();
    m_userRole = static_cast<UserRole>(userRoleInt);
    m_currentUsername = readVal("User/Username", "operator1").toString();
    m_currentUserFullName = readVal("User/FullName", "Masa 1 Operatörü").toString();

    m_hostIp = readVal("Network/HostIp", "127.0.0.1").toString();
    m_tcpPort = static_cast<quint16>(readVal("Network/TcpPort", 9100).toUInt());
    m_udpBeaconPort = static_cast<quint16>(readVal("Network/UdpPort", 9101).toUInt());

    m_dbHost = readVal("Database/Host", "127.0.0.1").toString();
    m_dbPort = readVal("Database/Port", 3306).toInt();
    m_dbName = readVal("Database/Name", "neta_antenna_test").toString();
    m_dbUser = readVal("Database/User", "root").toString();
    m_dbPassword = readVal("Database/Password", "neta123").toString();
    if (m_dbPassword.isEmpty()) {
        m_dbPassword = "neta123";
    }

    m_serialPort = readVal("Hardware/SerialPort", "COM1").toString();
    m_serialBaudRate = readVal("Hardware/BaudRate", 115200).toInt();

    m_lockTimeoutSec = readVal("Test/LockTimeoutSec", 90).toInt();
    m_sampleDurationSec = readVal("Test/SampleDurationSec", 5).toInt();
    m_defaultToleranceDb = readVal("Test/DefaultToleranceDb", 1.50).toDouble();
    m_reportDir = readVal("Report/OutputDir", "reports").toString();

    m_refDrxHost = readVal("DrxWeb/RefHost", "192.168.0.167").toString();
    m_refDrxPort = readVal("DrxWeb/RefPort", 80).toInt();
    m_stationDrxHost = readVal("DrxWeb/StationHost", "192.168.0.167").toString();
    m_stationDrxPort = readVal("DrxWeb/StationPort", 80).toInt();
    m_drxUsername = readVal("DrxWeb/Username", "admin").toString();
    m_drxPassword = readVal("DrxWeb/Password", "1234").toString();

    m_lastWorkOrderId = readVal("Session/LastWorkOrderId", 0).toInt();
    m_lastProfileId = readVal("Session/LastProfileId", 1).toInt();
    int refModeInt = readVal("Session/LastReferenceMode", static_cast<int>(ReferenceMode::LiveReference)).toInt();
    m_lastReferenceMode = (refModeInt == static_cast<int>(ReferenceMode::GoldenSample))
                              ? ReferenceMode::GoldenSample
                              : ReferenceMode::LiveReference;

    m_isLoading = false;

    if (!iniExists) {
        save();
    }
}

void AppConfig::save() {
    if (m_isLoading) return;

    QSettings settings(configFilePath(), QSettings::IniFormat);
    settings.setIniCodec("UTF-8");

    settings.setValue("General/Role", static_cast<int>(m_role));
    settings.setValue("General/StationId", m_stationId);
    settings.setValue("General/StationAlias", m_stationAlias);
    settings.setValue("General/ServerName", m_serverName);
    settings.setValue("User/Role", static_cast<int>(m_userRole));
    settings.setValue("User/Username", m_currentUsername);
    settings.setValue("User/FullName", m_currentUserFullName);

    settings.setValue("Network/HostIp", m_hostIp);
    settings.setValue("Network/TcpPort", m_tcpPort);
    settings.setValue("Network/UdpPort", m_udpBeaconPort);

    settings.setValue("Database/Host", m_dbHost);
    settings.setValue("Database/Port", m_dbPort);
    settings.setValue("Database/Name", m_dbName);
    settings.setValue("Database/User", m_dbUser);
    settings.setValue("Database/Password", m_dbPassword);

    settings.setValue("Hardware/SerialPort", m_serialPort);
    settings.setValue("Hardware/BaudRate", m_serialBaudRate);

    settings.setValue("Test/LockTimeoutSec", m_lockTimeoutSec);
    settings.setValue("Test/SampleDurationSec", m_sampleDurationSec);
    settings.setValue("Test/DefaultToleranceDb", m_defaultToleranceDb);
    settings.setValue("Report/OutputDir", m_reportDir);

    settings.setValue("DrxWeb/RefHost", m_refDrxHost);
    settings.setValue("DrxWeb/RefPort", m_refDrxPort);
    settings.setValue("DrxWeb/StationHost", m_stationDrxHost);
    settings.setValue("DrxWeb/StationPort", m_stationDrxPort);
    settings.setValue("DrxWeb/Username", m_drxUsername);
    settings.setValue("DrxWeb/Password", m_drxPassword);

    settings.setValue("Session/LastWorkOrderId", m_lastWorkOrderId);
    settings.setValue("Session/LastProfileId", m_lastProfileId);
    settings.setValue("Session/LastReferenceMode", static_cast<int>(m_lastReferenceMode));

    settings.sync();
}
