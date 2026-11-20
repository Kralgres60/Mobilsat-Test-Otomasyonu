/**
 * @file tcp_server.cpp
 * @brief Implementation of the transport-agnostic central Command & Mutual Exclusion Server.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "tcp_server.h"
#include "network_messages.h"
#include "tcp_transport.h"
#include "database/db_manager.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>

TcpServer::TcpServer(LockArbiter *arbiter, DataStream *refStream, QObject *parent)
    : QObject(parent), m_arbiter(arbiter) {
    m_ownedStream = new NetServerStream(this);
    m_stream = m_ownedStream;
    attachStreamSignals(m_stream);

    connect(&m_refPollTimer, &QTimer::timeout, this, &TcpServer::onRefPollTick);
    connect(&m_silenceWatchdogTimer, &QTimer::timeout, this, &TcpServer::onSilenceWatchdogTick);
    m_silenceWatchdogTimer.start(3000);

    if (m_arbiter) {
        connect(m_arbiter, &LockArbiter::lockGranted, this, &TcpServer::onLockGranted);
        connect(m_arbiter, &LockArbiter::lockQueued, this, &TcpServer::onLockQueued);
        connect(m_arbiter, &LockArbiter::lockReleased, this, &TcpServer::onLockReleased);
        connect(m_arbiter, &LockArbiter::lockExpired, this, &TcpServer::onLockExpired);
    }

    setRefDataStream(refStream);
}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::setStream(NetServerStream *stream) {
    if (m_stream == stream) return;
    if (m_stream) {
        disconnect(m_stream, nullptr, this, nullptr);
    }
    m_stream = stream ? stream : m_ownedStream;
    attachStreamSignals(m_stream);
}

void TcpServer::attachStreamSignals(NetServerStream *stream) {
    if (!stream) return;
    connect(stream, &NetServerStream::peerConnected, this, &TcpServer::onPeerConnected);
    connect(stream, &NetServerStream::dataReceived, this, &TcpServer::onPeerDataReceived);
    connect(stream, &NetServerStream::peerDisconnected, this, &TcpServer::onPeerDisconnected);
}

void TcpServer::ensureDefaultStream() {
    if (!m_stream) {
        m_stream = m_ownedStream;
        attachStreamSignals(m_stream);
    }
    if (m_stream != m_ownedStream || m_stream->isBound()) {
        return;
    }
    auto *transport = new TcpServerTransport(this);
    m_ownedTransport = transport;
    transport->attachStream(m_ownedStream);
}

void TcpServer::setRefDataStream(DataStream *refStream) {
    if (m_refStream == refStream) return;
    if (m_refStream) {
        disconnect(m_refStream, nullptr, this, nullptr);
    }
    m_refStream = refStream;
    if (m_refStream) {
        connect(m_refStream, &DataStream::statusReceived, this,
                [this](bool sig, bool vid, bool aud, const QString &) {
            onRefStatus(sig, vid, aud);
        });
        connect(m_refStream, &DataStream::telemetryReceived, this,
                [this](double powDbm, double cnDb, double ber, double) {
            onRefTelemetry(powDbm, cnDb, ber);
        });
    }
}

void TcpServer::setReferenceMode(ReferenceMode mode) {
    m_referenceMode = mode;
    const QString modeStr = (mode == ReferenceMode::GoldenSample) ? QStringLiteral("GOLDEN") : QStringLiteral("LIVE");
    const int activeGoldenId = DbManager::instance().getActiveGoldenRecordId();
    emit logMessage(QString("[Yönetici Modu] Referans kaynağı güncellendi: %1 (Aktif Golden ID: %2)")
                        .arg(mode == ReferenceMode::GoldenSample ? "Kayıtlı Golden Sample" : "Canlı Referans Anten")
                        .arg(activeGoldenId));

    QJsonObject msg;
    msg["type"] = NetMsg::TYPE_REF_MODE_UPDATE;
    msg["ref_mode"] = modeStr;
    msg["active_golden_id"] = activeGoldenId;
    broadcastJson(msg);
}

void TcpServer::broadcastUserRoleUpdates() {
    const auto users = DbManager::instance().getUsers();
    if (users.isEmpty()) return;

    QJsonArray vaultArr;
    for (const auto &u : users) {
        QJsonObject item;
        item["id"] = u.id;
        item["username"] = u.username;
        item["pwd_token"] = DbManager::deriveUserPasswordToken(
            u.username, u.password.isEmpty() ? QStringLiteral("123") : u.password);
        item["full_name"] = u.fullName;
        item["role"] = u.role;
        item["is_active"] = u.isActive;
        vaultArr.append(item);

        QJsonObject msg;
        msg["type"] = "ROLE_UPDATE";
        msg["username"] = u.username;
        msg["user_role"] = u.role;
        msg["user_full_name"] = u.fullName;
        msg["user_active"] = u.isActive;
        broadcastJson(msg);
    }

    QJsonObject syncMsg;
    syncMsg["type"] = "USERS_VAULT_SYNC";
    syncMsg["users_vault"] = vaultArr;
    broadcastJson(syncMsg);
}

bool TcpServer::start(quint16 port) {
    ensureDefaultStream();
    if (m_stream->start(port)) {
        emit logMessage(QString("TCP Command Server listening on port %1.").arg(port));
        return true;
    }
    emit logMessage(QString("Failed to start TCP Server: %1").arg(m_stream->errorString()));
    return false;
}

void TcpServer::stop() {
    m_refPollTimer.stop();
    m_silenceWatchdogTimer.stop();
    if (m_stream && m_stream->isBound()) {
        QJsonObject byeObj;
        byeObj["type"] = "SERVER_SHUTDOWN";
        broadcastJson(byeObj);
        m_stream->stop();
    }
    m_clients.clear();
}

bool TcpServer::isListening() const {
    return m_stream ? m_stream->isListening() : false;
}

QMap<int, StationLiveStatus> TcpServer::connectedStations() const {
    QMap<int, StationLiveStatus> result;
    for (const auto &session : m_clients) {
        if (session.stationId > 0) {
            result[session.stationId] = session.status;
        }
    }
    return result;
}

void TcpServer::onPeerConnected(quint64 peerId, const QString &ipAddress) {
    ClientSession session;
    session.ipAddress = ipAddress;
    session.status.ipAddress = ipAddress;
    session.status.isConnected = true;
    session.status.lastHeartbeat = QDateTime::currentDateTime();

    m_clients.insert(peerId, session);
    emit logMessage(QString("Accepted connection from %1").arg(session.ipAddress));
}

void TcpServer::onPeerDataReceived(quint64 peerId, const QByteArray &data) {
    if (!m_clients.contains(peerId)) return;

    auto &session = m_clients[peerId];
    session.rxBuffer.append(data);

    while (session.rxBuffer.contains('\n')) {
        int idx = session.rxBuffer.indexOf('\n');
        QByteArray line = session.rxBuffer.left(idx).trimmed();
        session.rxBuffer.remove(0, idx + 1);

        if (!line.isEmpty()) {
            QJsonObject obj = NetMsg::deserialize(line);
            if (!obj.isEmpty()) {
                processClientMessage(peerId, obj);
            }
        }
    }
}

void TcpServer::processClientMessage(quint64 peerId, const QJsonObject &msg) {
    if (!m_clients.contains(peerId)) return;
    auto &session = m_clients[peerId];
    const QString type = msg["type"].toString();

    if (type == NetMsg::TYPE_REGISTER) {
        int requestedId = msg["station_id"].toInt();

        auto isIdInUseByOther = [&](int candidate) {
            if (candidate <= 0) return true;
            for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
                if (it.key() != peerId && it.value().stationId == candidate) {
                    return true;
                }
            }
            return false;
        };

        int assignedId = requestedId;
        if (isIdInUseByOther(assignedId)) {
            assignedId = 1;
            while (isIdInUseByOther(assignedId)) {
                ++assignedId;
            }
        }

        session.stationId = assignedId;
        session.status.stationId = session.stationId;
        QString reqName = msg["station_name"].toString().trimmed();
        session.status.stationName = reqName.isEmpty() ? QString("İstasyon %1").arg(assignedId) : reqName;
        session.status.currentState = "IDLE";
        session.status.lastHeartbeat = QDateTime::currentDateTime();

        QJsonObject ack;
        ack["type"] = NetMsg::TYPE_REGISTER_ACK;
        ack["status"] = "OK";
        ack["assigned_station_id"] = session.stationId;
        ack["ref_mode"] = (m_referenceMode == ReferenceMode::GoldenSample) ? "GOLDEN" : "LIVE";
        ack["active_golden_id"] = DbManager::instance().getActiveGoldenRecordId();
        ack["server_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);

        const QString reqUser = msg["username"].toString().trimmed();
        const auto users = DbManager::instance().getUsers();
        if (!users.isEmpty()) {
            QJsonArray vaultArr;
            for (const auto &u : users) {
                QJsonObject item;
                item["id"] = u.id;
                item["username"] = u.username;
                item["pwd_token"] = DbManager::deriveUserPasswordToken(
                    u.username, u.password.isEmpty() ? QStringLiteral("123") : u.password);
                item["full_name"] = u.fullName;
                item["role"] = u.role;
                item["is_active"] = u.isActive;
                vaultArr.append(item);

                if (!reqUser.isEmpty() && u.username.compare(reqUser, Qt::CaseInsensitive) == 0) {
                    ack["user_role"] = u.role;
                    ack["user_full_name"] = u.fullName;
                    ack["user_active"] = u.isActive;
                }
            }
            ack["users_vault"] = vaultArr;
        }

        m_stream->writeToPeer(peerId, NetMsg::serialize(ack));

        emit stationConnected(session.stationId, session.ipAddress);
        emit stationStatusUpdated(session.status);
    }
    else if (type == NetMsg::TYPE_HEARTBEAT) {
        session.status.currentState = msg["state"].toString();
        session.status.currentChannel = msg["channel_no"].toInt();
        session.status.currentAntennaSn = msg["antenna_sn"].toString();
        session.status.lastHeartbeat = QDateTime::currentDateTime();
        emit stationStatusUpdated(session.status);
    }
    else if (type == NetMsg::TYPE_LOCK_REQ) {
        int stationId = msg["station_id"].toInt();
        int timeoutSec = msg["timeout_sec"].toInt(90);
        if (m_arbiter) {
            m_arbiter->requestLock(stationId, timeoutSec);
        }
    }
    else if (type == NetMsg::TYPE_REF_TUNE) {
        int freq = msg["freq"].toInt();
        int sr = msg["sr"].toInt();
        QString pol = msg["pol"].toString();
        int stationId = msg["station_id"].toInt();

        // Tune Reference DRX when requested by active lock holder (LiveReference or Hybrid Golden fallback)
        if (m_arbiter && m_arbiter->activeStationId() == stationId) {
            if (m_refStream && m_refStream->isConnected()) {
                m_refStream->sendTune(freq, sr, pol);
                m_refStream->requestStatus();
            }
            if (!m_refPollTimer.isActive()) {
                m_refPollTimer.start(300);
            }
        }
    }
    else if (type == NetMsg::TYPE_LOCK_RELEASE) {
        int stationId = msg["station_id"].toInt();
        if (m_arbiter) {
            m_arbiter->releaseLock(stationId);
        }
        QJsonObject ack;
        ack["type"] = NetMsg::TYPE_LOCK_RELEASED;
        m_stream->writeToPeer(peerId, NetMsg::serialize(ack));
    }
}

void TcpServer::onSilenceWatchdogTick() {
    if (!m_stream) return;
    const QDateTime now = QDateTime::currentDateTime();
    QList<quint64> stalePeers;
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        const ClientSession &session = it.value();
        if (session.status.lastHeartbeat.isValid() && session.status.lastHeartbeat.secsTo(now) > 10) {
            stalePeers.append(it.key());
        }
    }
    for (quint64 peerId : stalePeers) {
        emit logMessage(QString("Station %1 timed out (no heartbeat > 10s); disconnecting.")
                            .arg(m_clients.value(peerId).stationId));
        m_stream->disconnectPeer(peerId, true);
    }
}

void TcpServer::onPeerDisconnected(quint64 peerId) {
    if (!m_clients.contains(peerId)) return;

    auto session = m_clients.take(peerId);
    if (session.stationId > 0) {
        if (m_arbiter) {
            m_arbiter->releaseLock(session.stationId);
        }
        emit stationDisconnected(session.stationId);
    }
    emit logMessage(QString("Station disconnected: Station %1 (%2)").arg(session.stationId).arg(session.ipAddress));
}

void TcpServer::onLockGranted(int stationId, int timeoutSec) {
    m_lastRefCn = 0.0;
    m_lastRefPow = 0.0;
    m_lastRefSLocked = false;
    m_lastRefVLocked = false;
    m_lastRefALocked = false;

    QJsonObject msg;
    msg["type"] = NetMsg::TYPE_LOCK_GRANTED;
    msg["station_id"] = stationId;
    msg["timeout_sec"] = timeoutSec;
    sendToStation(stationId, msg);
}

void TcpServer::onLockQueued(int stationId, int position, int activeStationId) {
    QJsonObject msg;
    msg["type"] = NetMsg::TYPE_LOCK_WAITING;
    msg["station_id"] = stationId;
    msg["queue_position"] = position;
    msg["active_station_id"] = activeStationId;
    sendToStation(stationId, msg);
}

void TcpServer::onLockReleased(int /*stationId*/) {
    if (!m_arbiter || !m_arbiter->isLocked()) {
        m_refPollTimer.stop();
    }
}

void TcpServer::onLockExpired(int stationId) {
    if (!m_arbiter || !m_arbiter->isLocked()) {
        m_refPollTimer.stop();
    }
    QJsonObject msg;
    msg["type"] = NetMsg::TYPE_LOCK_EXPIRED;
    msg["station_id"] = stationId;
    msg["reason"] = "Maximum test duration exceeded.";
    sendToStation(stationId, msg);
}

void TcpServer::onRefPollTick() {
    if (!m_arbiter || !m_arbiter->isLocked()) {
        m_refPollTimer.stop();
        return;
    }
    if (m_refStream && m_refStream->isConnected()) {
        m_refStream->requestStatus();
    }
}

void TcpServer::onRefTelemetry(double powDbm, double cnDb, double /*ber*/) {
    m_lastRefPow = powDbm;
    m_lastRefCn = cnDb;

    // Stream live reference telemetry to the currently active station
    if (m_arbiter && m_arbiter->isLocked()) {
        int activeStation = m_arbiter->activeStationId();
        QJsonObject msg;
        msg["type"] = NetMsg::TYPE_REF_DATA;
        msg["station_id"] = activeStation;
        msg["ref_cn"] = cnDb;
        msg["ref_pow"] = powDbm;
        msg["carrier_locked"] = m_lastRefSLocked;
        msg["video_locked"] = m_lastRefVLocked;
        sendToStation(activeStation, msg);
    }
}

void TcpServer::onRefStatus(bool sLocked, bool vLocked, bool aLocked) {
    m_lastRefSLocked = sLocked;
    m_lastRefVLocked = vLocked;
    m_lastRefALocked = aLocked;
}

void TcpServer::sendToStation(int stationId, const QJsonObject &msg) {
    if (!m_stream) return;
    const quint64 peerId = findPeerByStationId(stationId);
    if (peerId != 0) {
        m_stream->writeToPeer(peerId, NetMsg::serialize(msg));
    }
}

void TcpServer::broadcastJson(const QJsonObject &msg) {
    if (!m_stream) return;
    const QByteArray payload = NetMsg::serialize(msg);
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        m_stream->writeToPeer(it.key(), payload);
    }
}

quint64 TcpServer::findPeerByStationId(int stationId) const {
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        if (it.value().stationId == stationId) {
            return it.key();
        }
    }
    return 0;
}
