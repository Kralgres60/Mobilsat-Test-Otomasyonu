/**
 * @file tcp_client.cpp
 * @brief Implementation of the station TCP client.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "tcp_client.h"
#include "network_messages.h"
#include <QDebug>

TcpClient::TcpClient(QObject *parent) : QObject(parent) {
    connect(&m_socket, &QTcpSocket::connected, this, &TcpClient::onConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, &TcpClient::onDisconnected);
    connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &TcpClient::onErrorOccurred);

    connect(&m_heartbeatTimer, &QTimer::timeout, this, &TcpClient::onHeartbeatTimer);
}

TcpClient::~TcpClient() {
    disconnectFromHost();
}

void TcpClient::connectToHost(const QString &hostIp, quint16 port) {
    m_hostIp = hostIp;
    m_port = port;
    if (m_socket.isOpen()) {
        m_socket.abort();
    }
    m_socket.connectToHost(hostIp, port);
}

void TcpClient::disconnectFromHost() {
    m_heartbeatTimer.stop();
    if (m_socket.isOpen()) {
        m_socket.disconnectFromHost();
    }
}

bool TcpClient::isConnected() const {
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void TcpClient::registerStation(int stationId, const QString &stationName) {
    m_stationId = stationId;
    m_stationName = stationName;

    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_REGISTER;
    obj["station_id"] = stationId;
    obj["station_name"] = stationName;
    sendJson(obj);
}

void TcpClient::sendHeartbeat(const QString &state, int currentChannel, const QString &antennaSn) {
    if (!isConnected()) return;
    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_HEARTBEAT;
    obj["station_id"] = m_stationId;
    obj["state"] = state;
    obj["channel_no"] = currentChannel;
    obj["antenna_sn"] = antennaSn;
    sendJson(obj);
}

void TcpClient::requestLock(int timeoutSec) {
    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_LOCK_REQ;
    obj["station_id"] = m_stationId;
    obj["timeout_sec"] = timeoutSec;
    sendJson(obj);
}

void TcpClient::releaseLock() {
    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_LOCK_RELEASE;
    obj["station_id"] = m_stationId;
    sendJson(obj);
}

void TcpClient::requestRefTune(int channelNo, int freqMHz, int symbolRate, const QString &polarity) {
    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_REF_TUNE;
    obj["station_id"] = m_stationId;
    obj["channel_no"] = channelNo;
    obj["freq"] = freqMHz;
    obj["sr"] = symbolRate;
    obj["pol"] = polarity;
    sendJson(obj);
}

void TcpClient::onConnected() {
    emit connected();
    emit logMessage(QString("Connected to Central Host: %1:%2").arg(m_hostIp).arg(m_port));
    m_heartbeatTimer.start(3000); // 3-second heartbeat interval
    registerStation(m_stationId, m_stationName);
}

void TcpClient::onReadyRead() {
    m_rxBuffer.append(m_socket.readAll());
    while (m_rxBuffer.contains('\n')) {
        int idx = m_rxBuffer.indexOf('\n');
        QByteArray line = m_rxBuffer.left(idx).trimmed();
        m_rxBuffer.remove(0, idx + 1);

        if (!line.isEmpty()) {
            QJsonObject obj = NetMsg::deserialize(line);
            if (!obj.isEmpty()) {
                processMessage(obj);
            }
        }
    }
}

void TcpClient::processMessage(const QJsonObject &msg) {
    QString type = msg["type"].toString();

    if (type == NetMsg::TYPE_REGISTER_ACK) {
        if (msg.contains("ref_mode")) {
            QString modeStr = msg["ref_mode"].toString();
            m_referenceMode = (modeStr.compare("GOLDEN", Qt::CaseInsensitive) == 0)
                                  ? ReferenceMode::GoldenSample
                                  : ReferenceMode::LiveReference;
            emit referenceModeChanged(m_referenceMode);
        }
        emit registered();
        emit logMessage("Host registration acknowledged.");
    }
    else if (type == NetMsg::TYPE_REF_MODE_UPDATE) {
        QString modeStr = msg["ref_mode"].toString();
        m_referenceMode = (modeStr.compare("GOLDEN", Qt::CaseInsensitive) == 0)
                              ? ReferenceMode::GoldenSample
                              : ReferenceMode::LiveReference;
        emit referenceModeChanged(m_referenceMode);
        emit logMessage(QString("Yönetici Referans Modunu güncelledi: %1")
                            .arg(m_referenceMode == ReferenceMode::GoldenSample
                                     ? "Kayıtlı Golden Sample"
                                     : "Canlı Referans Anten"));
    }
    else if (type == NetMsg::TYPE_LOCK_GRANTED) {
        int timeoutSec = msg["timeout_sec"].toInt(90);
        emit lockGranted(timeoutSec);
        emit logMessage(QString("Reference hardware lock granted! Lease timeout: %1 sec").arg(timeoutSec));
    }
    else if (type == NetMsg::TYPE_LOCK_WAITING) {
        int pos = msg["queue_position"].toInt();
        int activeStation = msg["active_station_id"].toInt();
        emit lockWaiting(pos, activeStation);
        emit logMessage(QString("Reference busy (Station %1 active). Queue position: %2").arg(activeStation).arg(pos));
    }
    else if (type == NetMsg::TYPE_LOCK_EXPIRED) {
        QString reason = msg["reason"].toString();
        emit lockExpired(reason);
        emit logMessage(QString("Reference lock expired: %1").arg(reason));
    }
    else if (type == NetMsg::TYPE_REF_DATA) {
        double refCn = msg["ref_cn"].toDouble();
        double refPow = msg["ref_pow"].toDouble();
        bool cLocked = msg["carrier_locked"].toBool();
        bool vLocked = msg["video_locked"].toBool();
        emit refDataReceived(refCn, refPow, cLocked, vLocked);
    }
}

void TcpClient::onDisconnected() {
    m_heartbeatTimer.stop();
    emit disconnected();
    emit logMessage("Disconnected from Central Host.");
}

void TcpClient::onErrorOccurred(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    emit logMessage(QString("TCP Socket error: %1").arg(m_socket.errorString()));
}

void TcpClient::onHeartbeatTimer() {
    sendHeartbeat("CONNECTED");
}

void TcpClient::sendJson(const QJsonObject &obj) {
    if (isConnected()) {
        m_socket.write(NetMsg::serialize(obj));
    }
}
