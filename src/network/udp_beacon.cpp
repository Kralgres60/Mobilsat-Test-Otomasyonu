/**
 * @file udp_beacon.cpp
 * @brief Implementation of transport-agnostic discovery Broadcaster and Listener.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "udp_beacon.h"
#include "network_messages.h"
#include "udp_transport.h"
#include "core/appinfo.h"
#include <QHostInfo>
#include <QJsonObject>

// --- Broadcaster (Host) ---
UdpBeaconBroadcaster::UdpBeaconBroadcaster(quint16 beaconPort, quint16 tcpPort, QObject *parent)
    : QObject(parent), m_beaconPort(beaconPort), m_tcpPort(tcpPort) {
    m_ownedStream = new NetDatagramStream(this);
    m_stream = m_ownedStream;
    connect(&m_timer, &QTimer::timeout, this, &UdpBeaconBroadcaster::sendBeacon);
}

void UdpBeaconBroadcaster::setStream(NetDatagramStream *stream) {
    m_stream = stream ? stream : m_ownedStream;
}

void UdpBeaconBroadcaster::ensureDefaultStream() {
    if (!m_stream) {
        m_stream = m_ownedStream;
    }
    if (m_stream != m_ownedStream || m_stream->isBound()) {
        return;
    }
    auto *transport = new UdpTransport(this);
    m_ownedTransport = transport;
    transport->attachStream(m_ownedStream);
}

void UdpBeaconBroadcaster::start(int intervalMs) {
    ensureDefaultStream();
    sendBeacon();
    m_timer.start(intervalMs);
}

void UdpBeaconBroadcaster::stop() {
    m_timer.stop();
    if (!m_stream || !m_stream->isBound()) return;
    QJsonObject obj;
    obj["type"] = "BEACON_BYE";
    obj["tcp_port"] = m_tcpPort;
    m_stream->writeBroadcast(NetMsg::serialize(obj), m_beaconPort);
}

void UdpBeaconBroadcaster::sendBeacon() {
    ensureDefaultStream();
    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_BEACON;
    obj["server_name"] = m_serverName.isEmpty() ? QHostInfo::localHostName() : m_serverName;
    obj["tcp_port"] = m_tcpPort;
    obj["station_count"] = m_stationCount;
    obj["version"] = NETA_APP_VERSION;

    m_stream->writeBroadcast(NetMsg::serialize(obj), m_beaconPort);
}

// --- Listener (Client) ---
UdpBeaconListener::UdpBeaconListener(quint16 beaconPort, QObject *parent)
    : QObject(parent), m_beaconPort(beaconPort) {
    m_ownedStream = new NetDatagramStream(this);
    m_stream = m_ownedStream;
    connect(m_stream, &NetDatagramStream::datagramReceived,
            this, &UdpBeaconListener::onDatagramReceived);
}

void UdpBeaconListener::setStream(NetDatagramStream *stream) {
    if (m_stream == stream) return;
    if (m_stream) {
        disconnect(m_stream, nullptr, this, nullptr);
    }
    m_stream = stream ? stream : m_ownedStream;
    connect(m_stream, &NetDatagramStream::datagramReceived,
            this, &UdpBeaconListener::onDatagramReceived);
}

void UdpBeaconListener::ensureDefaultStream() {
    if (!m_stream) {
        m_stream = m_ownedStream;
        connect(m_stream, &NetDatagramStream::datagramReceived,
                this, &UdpBeaconListener::onDatagramReceived);
    }
    if (m_stream != m_ownedStream || m_stream->isBound()) {
        return;
    }
    auto *transport = new UdpTransport(this);
    m_ownedTransport = transport;
    transport->attachStream(m_ownedStream);
}

bool UdpBeaconListener::start() {
    ensureDefaultStream();
    return m_stream->bindPort(m_beaconPort);
}

void UdpBeaconListener::stop() {
    if (m_stream && m_stream->isBound()) {
        m_stream->close();
    }
}

void UdpBeaconListener::onDatagramReceived(const QByteArray &data, const QString &senderIp) {
    const QJsonObject obj = NetMsg::deserialize(data);
    const QString msgType = obj["type"].toString();
    if (msgType == NetMsg::TYPE_BEACON || msgType == "BEACON_BYE") {
        const quint16 tcpPort = static_cast<quint16>(obj["tcp_port"].toInt(9100));

        if (msgType == "BEACON_BYE") {
            emit hostLost(senderIp, tcpPort);
            return;
        }

        const QString serverName = obj["server_name"].toString();
        const int stationCount = obj["station_count"].toInt(0);
        const QString version = obj["version"].toString();

        emit hostDiscovered(senderIp, serverName, tcpPort);
        emit hostDiscoveredInfo(senderIp, serverName, tcpPort, stationCount, version);
    }
}
