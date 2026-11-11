/**
 * @file udp_beacon.h
 * @brief Zero-configuration LAN UDP discovery broadcaster and listener (Architecture 1).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef UDP_BEACON_H
#define UDP_BEACON_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>

/**
 * @class UdpBeaconBroadcaster
 * @brief Broadcasts periodic UDP beacon frames over local subnet for zero-configuration station discovery.
 */
class UdpBeaconBroadcaster : public QObject {
    Q_OBJECT
public:
    explicit UdpBeaconBroadcaster(quint16 beaconPort, quint16 tcpPort, QObject *parent = nullptr);
    void start(int intervalMs = 2000);
    void stop();
    void setServerName(const QString &name) { m_serverName = name; }
    void setStationCount(int count) { m_stationCount = count; }

private slots:
    void sendBeacon();

private:
    QUdpSocket m_socket;
    QTimer m_timer;
    quint16 m_beaconPort;
    quint16 m_tcpPort;
    QString m_serverName;
    int m_stationCount = 0;
};

/**
 * @class UdpBeaconListener
 * @brief Listens for Host discovery beacons on the local subnet to automatically locate the Host IP.
 */
class UdpBeaconListener : public QObject {
    Q_OBJECT
public:
    explicit UdpBeaconListener(quint16 beaconPort, QObject *parent = nullptr);
    bool start();
    void stop();

signals:
    void hostDiscovered(const QString &hostIp, const QString &serverName, quint16 tcpPort);
    void hostDiscoveredInfo(const QString &hostIp, const QString &serverName, quint16 tcpPort, int stationCount, const QString &version);

private slots:
    void onReadyRead();

private:
    QUdpSocket m_socket;
    quint16 m_beaconPort;
};

#endif // UDP_BEACON_H
