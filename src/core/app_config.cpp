/**
 * @file app_config.cpp
 * @brief Implementation of AppConfig persistent settings loader and saver.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "app_config.h"

AppConfig& AppConfig::instance() {
    static AppConfig s_instance;
    return s_instance;
}

AppConfig::AppConfig() {
    load();
}

void AppConfig::load() {
    QSettings settings("NETA", "DRX_Antenna_Test");

    int roleInt = settings.value("General/Role", static_cast<int>(AppRole::Client)).toInt();
    m_role = static_cast<AppRole>(roleInt);
    m_stationId = settings.value("General/StationId", 1).toInt();

    m_hostIp = settings.value("Network/HostIp", "127.0.0.1").toString();
    m_tcpPort = static_cast<quint16>(settings.value("Network/TcpPort", 9100).toUInt());
    m_udpBeaconPort = static_cast<quint16>(settings.value("Network/UdpPort", 9101).toUInt());

    m_dbHost = settings.value("Database/Host", "127.0.0.1").toString();
    m_dbPort = settings.value("Database/Port", 3306).toInt();
    m_dbName = settings.value("Database/Name", "neta_antenna_test").toString();
    m_dbUser = settings.value("Database/User", "root").toString();
    m_dbPassword = settings.value("Database/Password", "neta123").toString();
    if (m_dbPassword.isEmpty()) {
        m_dbPassword = "neta123";
    }

    m_serialPort = settings.value("Hardware/SerialPort", "COM1").toString();
    m_serialBaudRate = settings.value("Hardware/BaudRate", 115200).toInt();

    m_lockTimeoutSec = settings.value("Test/LockTimeoutSec", 90).toInt();
    m_sampleDurationSec = settings.value("Test/SampleDurationSec", 5).toInt();
    m_defaultToleranceDb = settings.value("Test/DefaultToleranceDb", 1.50).toDouble();
    m_reportDir = settings.value("Report/OutputDir", "reports").toString();

    m_refDrxHost = settings.value("DrxWeb/RefHost", "192.168.0.167").toString();
    m_refDrxPort = settings.value("DrxWeb/RefPort", 80).toInt();
    m_stationDrxHost = settings.value("DrxWeb/StationHost", "192.168.0.167").toString();
    m_stationDrxPort = settings.value("DrxWeb/StationPort", 80).toInt();
    m_drxUsername = settings.value("DrxWeb/Username", "admin").toString();
    m_drxPassword = settings.value("DrxWeb/Password", "1234").toString();
}

void AppConfig::save() {
    QSettings settings("NETA", "DRX_Antenna_Test");

    settings.setValue("General/Role", static_cast<int>(m_role));
    settings.setValue("General/StationId", m_stationId);

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

    settings.sync();
}
