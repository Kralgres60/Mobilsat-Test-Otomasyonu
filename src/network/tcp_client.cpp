/**
 * @file tcp_client.cpp
 * @brief Implementation of the transport-agnostic station protocol client.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "tcp_client.h"
#include "network_messages.h"
#include "tcp_transport.h"
#include "core/app_config.h"
#include "database/db_manager.h"
#include <QDebug>
#include <QJsonArray>

namespace {

void applyRemoteUsersVault(const QJsonArray &arr) {
    if (arr.isEmpty()) return;
    QVector<UserRecord> users;
    users.reserve(arr.size());
    for (const QJsonValue &val : arr) {
        if (!val.isObject()) continue;
        const QJsonObject o = val.toObject();
        UserRecord u;
        u.id = o["id"].toInt(0);
        u.username = o["username"].toString().trimmed();
        u.password = o["pwd_token"].toString().trimmed();
        u.fullName = o["full_name"].toString().trimmed();
        u.role = o["role"].toString().trimmed().toUpper();
        u.isActive = o["is_active"].toBool(true);
        if (!u.username.isEmpty() && !u.password.isEmpty()) {
            users.append(u);
        }
    }
    if (!users.isEmpty()) {
        DbManager::instance().syncLocalUserVault(users);
    }
}

} // namespace

TcpClient::TcpClient(QObject *parent) : QObject(parent) {
    m_ownedStream = new NetClientStream(this);
    m_stream = m_ownedStream;
    attachStreamSignals(m_stream);

    connect(&m_heartbeatTimer, &QTimer::timeout, this, &TcpClient::onHeartbeatTimer);
}

TcpClient::~TcpClient() {
    disconnectFromHost();
}

void TcpClient::setStream(NetClientStream *stream) {
    if (m_stream == stream) return;
    if (m_stream) {
        disconnect(m_stream, nullptr, this, nullptr);
    }
    m_stream = stream ? stream : m_ownedStream;
    attachStreamSignals(m_stream);
}

void TcpClient::attachStreamSignals(NetClientStream *stream) {
    if (!stream) return;
    connect(stream, &NetClientStream::connected, this, &TcpClient::onConnected);
    connect(stream, &NetClientStream::dataReceived, this, &TcpClient::onDataReceived);
    connect(stream, &NetClientStream::disconnected, this, &TcpClient::onDisconnected);
    connect(stream, &NetClientStream::errorOccurred, this, &TcpClient::onErrorOccurred);
}

void TcpClient::ensureDefaultStream() {
    if (!m_stream) {
        m_stream = m_ownedStream;
        attachStreamSignals(m_stream);
    }
    if (m_stream != m_ownedStream || m_stream->isBound()) {
        return;
    }
    auto *transport = new TcpSocketTransport(this);
    m_ownedTransport = transport;
    transport->attachStream(m_ownedStream);
}

void TcpClient::connectToHost(const QString &hostIp, quint16 port) {
    ensureDefaultStream();
    m_hostIp = hostIp;
    m_port = port;
    m_stream->connectToHost(hostIp, port);
}

void TcpClient::disconnectFromHost() {
    m_heartbeatTimer.stop();
    if (m_stream && m_stream->isBound()) {
        m_stream->disconnectFromHost();
    }
}

bool TcpClient::isConnected() const {
    return m_stream ? m_stream->isConnected() : false;
}

void TcpClient::registerStation(int stationId, const QString &stationName) {
    m_stationId = stationId;
    m_stationName = stationName;

    QJsonObject obj;
    obj["type"] = NetMsg::TYPE_REGISTER;
    obj["station_id"] = stationId;
    obj["station_name"] = stationName;
    obj["username"] = AppConfig::instance().currentUsername();
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

void TcpClient::onDataReceived(const QByteArray &data) {
    m_rxBuffer.append(data);
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
        if (msg.contains("assigned_station_id")) {
            int assigned = msg["assigned_station_id"].toInt();
            if (assigned > 0) {
                m_stationId = assigned;
                emit stationIdAssigned(assigned);
            }
        }
        if (msg.contains("active_golden_id")) {
            m_activeGoldenRecordId = msg["active_golden_id"].toInt(0);
        }
        if (msg.contains("ref_mode")) {
            QString modeStr = msg["ref_mode"].toString();
            m_referenceMode = (modeStr.compare("GOLDEN", Qt::CaseInsensitive) == 0)
                                  ? ReferenceMode::GoldenSample
                                  : ReferenceMode::LiveReference;
            emit referenceModeChanged(m_referenceMode);
        }
        if (msg.contains("users_vault") && msg["users_vault"].isArray()) {
            applyRemoteUsersVault(msg["users_vault"].toArray());
        }
        if (msg.contains("user_role")) {
            emit userAuthorizationReceived(msg["user_role"].toString(),
                                           msg["user_full_name"].toString(),
                                           msg["user_active"].toBool(true));
        }
        emit registered();
        emit logMessage(QString("Host registration acknowledged (Assigned ID: %1).").arg(m_stationId));
    }
    else if (type == NetMsg::TYPE_REF_MODE_UPDATE) {
        if (msg.contains("active_golden_id")) {
            m_activeGoldenRecordId = msg["active_golden_id"].toInt(0);
        }
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
    else if (type == "ROLE_UPDATE") {
        const QString targetUser = msg["username"].toString();
        if (targetUser.compare(AppConfig::instance().currentUsername(), Qt::CaseInsensitive) == 0) {
            emit userAuthorizationReceived(msg["user_role"].toString(),
                                           msg["user_full_name"].toString(),
                                           msg["user_active"].toBool(true));
        }
    }
    else if (type == "USERS_VAULT_SYNC") {
        if (msg.contains("users_vault") && msg["users_vault"].isArray()) {
            applyRemoteUsersVault(msg["users_vault"].toArray());
        }
    }
    else if (type == "SERVER_SHUTDOWN") {
        emit logMessage("Yönetici Sunucu (Host) kapatıldı!");
        emit serverShutdown();
        disconnectFromHost();
    }
}

void TcpClient::onDisconnected() {
    m_heartbeatTimer.stop();
    emit disconnected();
    emit logMessage("Disconnected from Central Host.");
}

void TcpClient::onErrorOccurred(const QString &error) {
    emit logMessage(QString("TCP Socket error: %1").arg(error));
}

void TcpClient::onHeartbeatTimer() {
    sendHeartbeat("CONNECTED");
}

void TcpClient::sendJson(const QJsonObject &obj) {
    if (isConnected()) {
        m_stream->write(NetMsg::serialize(obj));
    }
}
