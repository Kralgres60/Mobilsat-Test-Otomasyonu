/**
 * @file udp_beacon.h
 * @brief Transport-agnostic zero-configuration LAN discovery broadcaster and listener (Architecture 1).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef UDP_BEACON_H
#define UDP_BEACON_H

#include <QObject>
#include <QTimer>
#include "net_stream.h"

/**
 * @class UdpBeaconBroadcaster
 * @brief Broadcasts periodic discovery beacon frames via NetDatagramStream.
 */
class UdpBeaconBroadcaster : public QObject {
    Q_OBJECT
public:
    explicit UdpBeaconBroadcaster(quint16 beaconPort, quint16 tcpPort, QObject *parent = nullptr);

    /** @brief Binds an external NetDatagramStream. */
    void setStream(NetDatagramStream *stream);

    void start(int intervalMs = 2000);
    void stop();
    void setServerName(const QString &name) { m_serverName = name; }
    void setStationCount(int count) { m_stationCount = count; }

private slots:
    void sendBeacon();

private:
    void ensureDefaultStream();

    NetDatagramStream *m_stream = nullptr;
    NetDatagramStream *m_ownedStream = nullptr;
    QObject           *m_ownedTransport = nullptr;
    QTimer m_timer;
    quint16 m_beaconPort;
    quint16 m_tcpPort;
    QString m_serverName;
    int m_stationCount = 0;
};

/**
 * @class UdpBeaconListener
 * @brief Listens for Host discovery beacons via NetDatagramStream to locate the Host IP.
 */
class UdpBeaconListener : public QObject {
    Q_OBJECT
public:
    explicit UdpBeaconListener(quint16 beaconPort, QObject *parent = nullptr);

    /** @brief Binds an external NetDatagramStream. */
    void setStream(NetDatagramStream *stream);

    bool start();
    void stop();

signals:
    void hostDiscovered(const QString &hostIp, const QString &serverName, quint16 tcpPort);
    void hostDiscoveredInfo(const QString &hostIp, const QString &serverName, quint16 tcpPort, int stationCount, const QString &version);
    /** @brief Emitted when a Host server broadcasts a graceful BEACON_BYE shutdown datagram. */
    void hostLost(const QString &hostIp, quint16 tcpPort);

private slots:
    void onDatagramReceived(const QByteArray &data, const QString &senderIp);

private:
    void ensureDefaultStream();

    NetDatagramStream *m_stream = nullptr;
    NetDatagramStream *m_ownedStream = nullptr;
    QObject           *m_ownedTransport = nullptr;
    quint16 m_beaconPort;
};

#endif // UDP_BEACON_H
