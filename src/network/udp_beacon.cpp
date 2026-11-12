/**
 * @file udp_beacon.cpp
 * @brief Implementation of UDP Discovery Broadcaster and Listener.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "udp_beacon.h"
#include "network_messages.h"
#include "core/appinfo.h"
#include <QHostInfo>
#include <QJsonObject>
#include <QNetworkDatagram>

// --- Broadcaster (Host) ---
UdpBeaconBroadcaster::UdpBeaconBroadcaster(quint16 beaconPort, quint16 tcpPort, QObject *parent)
    : QObject(parent), m_beaconPort(beaconPort), m_tcpPort(tcpPort) {
    connect(&m_timer, &QTimer::timeout, this, &UdpBeaconBroadcaster::sendBeacon);
}

void UdpBeaconBroadcaster::start(int intervalMs) {
    sendBeacon();
    m_timer.start(intervalMs);
}

void UdpBeaconBroadcaster::stop() {
    m_timer.stop();
    // Broadcast a graceful goodbye datagram so open lobbies remove this server immediately
    QJsonObject obj;
    obj["type"] = "BEACON_BYE";
    obj["tcp_port"] = m_tcpPort;
    m_socket.writeDatagram(NetMsg::serialize(obj), QHostAddress::Broadcast, m_beaconPort);
    m_socket.flush();
}

void UdpBeaconBroadcaster::sendBeacon() {
    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_BEACON;
    obj["server_name"] = m_serverName.isEmpty() ? QHostInfo::localHostName() : m_serverName;
    obj["tcp_port"] = m_tcpPort;
    obj["station_count"] = m_stationCount;
    obj["version"] = NETA_APP_VERSION;

    QByteArray datagram = NetMsg::serialize(obj);
    m_socket.writeDatagram(datagram, QHostAddress::Broadcast, m_beaconPort);
}

// --- Listener (Client) ---
UdpBeaconListener::UdpBeaconListener(quint16 beaconPort, QObject *parent)
    : QObject(parent), m_beaconPort(beaconPort) {
    connect(&m_socket, &QUdpSocket::readyRead, this, &UdpBeaconListener::onReadyRead);
}

bool UdpBeaconListener::start() {
    return m_socket.bind(m_beaconPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
}

void UdpBeaconListener::stop() {
    m_socket.close();
}

void UdpBeaconListener::onReadyRead() {
    while (m_socket.hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket.receiveDatagram();
        QByteArray data = datagram.data();
        QJsonObject obj = NetMsg::deserialize(data);

        const QString msgType = obj["type"].toString();
        if (msgType == NetMsg::TYPE_BEACON || msgType == "BEACON_BYE") {
            QString hostIp = datagram.senderAddress().toString();
            // Normalize IPv6-mapped IPv4 addresses (e.g., ::ffff:192.168.1.50 -> 192.168.1.50)
            if (hostIp.startsWith("::ffff:")) {
                hostIp = hostIp.mid(7);
            }
            quint16 tcpPort = static_cast<quint16>(obj["tcp_port"].toInt(9100));

            if (msgType == "BEACON_BYE") {
                emit hostLost(hostIp, tcpPort);
                continue;
            }

            QString serverName = obj["server_name"].toString();
            int stationCount = obj["station_count"].toInt(0);
            QString version = obj["version"].toString();

            emit hostDiscovered(hostIp, serverName, tcpPort);
            emit hostDiscoveredInfo(hostIp, serverName, tcpPort, stationCount, version);
        }
    }
}
