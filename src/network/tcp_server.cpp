/**
 * @file tcp_server.cpp
 * @brief Implementation of the central TCP Command & Mutual Exclusion Server.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "tcp_server.h"
#include "network_messages.h"
#include <QDateTime>
#include <QDebug>

TcpServer::TcpServer(LockArbiter *arbiter, SerialWorker *refSerial, QObject *parent)
    : QObject(parent), m_arbiter(arbiter), m_refSerial(refSerial) {
    connect(&m_server, &QTcpServer::newConnection, this, &TcpServer::onNewConnection);
    connect(&m_refPollTimer, &QTimer::timeout, this, &TcpServer::onRefPollTick);
    connect(&m_silenceWatchdogTimer, &QTimer::timeout, this, &TcpServer::onSilenceWatchdogTick);
    m_silenceWatchdogTimer.start(3000);

    if (m_arbiter) {
        connect(m_arbiter, &LockArbiter::lockGranted, this, &TcpServer::onLockGranted);
        connect(m_arbiter, &LockArbiter::lockQueued, this, &TcpServer::onLockQueued);
        connect(m_arbiter, &LockArbiter::lockReleased, this, &TcpServer::onLockReleased);
        connect(m_arbiter, &LockArbiter::lockExpired, this, &TcpServer::onLockExpired);
    }

    if (m_refSerial) {
        connect(m_refSerial, &SerialWorker::telemetryReceived, this, &TcpServer::onRefTelemetry);
        connect(m_refSerial, &SerialWorker::statusReceived, this, &TcpServer::onRefStatus);
    }
}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::setRefWebClient(DrxWebClient *refWebClient) {
    if (m_refWebClient == refWebClient) return;
    m_refWebClient = refWebClient;
    if (m_refWebClient) {
        connect(m_refWebClient, &DrxWebClient::statusReceived, this,
                [this](bool sig, bool vid, bool aud, const QString &) {
            onRefStatus(sig, vid, aud);
        });
        connect(m_refWebClient, &DrxWebClient::telemetryReceived, this,
                [this](double powDbm, double cnDb, double ber, double) {
            onRefTelemetry(powDbm, cnDb, ber);
        });
    }
}

void TcpServer::setReferenceMode(ReferenceMode mode) {
    m_referenceMode = mode;
    QString modeStr = (mode == ReferenceMode::GoldenSample) ? "GOLDEN" : "LIVE";
    emit logMessage(QString("[Yönetici Modu] Referans kaynağı güncellendi: %1")
                        .arg(mode == ReferenceMode::GoldenSample ? "Kayıtlı Golden Sample" : "Canlı Referans Anten"));

    QJsonObject msg;
    msg["type"] = NetMsg::TYPE_REF_MODE_UPDATE;
    msg["ref_mode"] = modeStr;
    const QByteArray payload = NetMsg::serialize(msg);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.key() && it.key()->isOpen()) {
            it.key()->write(payload);
        }
    }
}

bool TcpServer::start(quint16 port) {
    if (m_server.listen(QHostAddress::Any, port)) {
        emit logMessage(QString("TCP Command Server listening on port %1.").arg(port));
        return true;
    }
    emit logMessage(QString("Failed to start TCP Server: %1").arg(m_server.errorString()));
    return false;
}

void TcpServer::stop() {
    m_refPollTimer.stop();
    m_silenceWatchdogTimer.stop();
    QJsonObject byeObj;
    byeObj["type"] = "SERVER_SHUTDOWN";
    const QByteArray byePayload = NetMsg::serialize(byeObj);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QTcpSocket *sock = it.key();
        if (sock) {
            sock->write(byePayload);
            sock->flush();
            sock->disconnectFromHost();
            if (sock->state() != QAbstractSocket::UnconnectedState) {
                sock->close();
            }
        }
    }
    m_clients.clear();
    m_server.close();
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

void TcpServer::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        ClientSession session;
        session.ipAddress = socket->peerAddress().toString();
        if (session.ipAddress.startsWith("::ffff:")) session.ipAddress = session.ipAddress.mid(7);
        session.status.ipAddress = session.ipAddress;
        session.status.isConnected = true;
        session.status.lastHeartbeat = QDateTime::currentDateTime();

        m_clients.insert(socket, session);

        connect(socket, &QTcpSocket::readyRead, this, &TcpServer::onClientReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &TcpServer::onClientDisconnected);

        emit logMessage(QString("Accepted connection from %1").arg(session.ipAddress));
    }
}

void TcpServer::onClientReadyRead() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !m_clients.contains(socket)) return;

    auto &session = m_clients[socket];
    session.rxBuffer.append(socket->readAll());

    while (session.rxBuffer.contains('\n')) {
        int idx = session.rxBuffer.indexOf('\n');
        QByteArray line = session.rxBuffer.left(idx).trimmed();
        session.rxBuffer.remove(0, idx + 1);

        if (!line.isEmpty()) {
            QJsonObject obj = NetMsg::deserialize(line);
            if (!obj.isEmpty()) {
                processClientMessage(socket, obj);
            }
        }
    }
}

void TcpServer::processClientMessage(QTcpSocket *socket, const QJsonObject &msg) {
    if (!m_clients.contains(socket)) return;
    auto &session = m_clients[socket];
    QString type = msg["type"].toString();

    if (type == NetMsg::TYPE_REGISTER) {
        int requestedId = msg["station_id"].toInt();

        auto isIdInUseByOther = [&](int candidate) {
            if (candidate <= 0) return true;
            for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
                if (it.key() != socket && it.value().stationId == candidate) {
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
        ack["server_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        socket->write(NetMsg::serialize(ack));

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

        // Tune Reference DRX only in LiveReference mode and when requested by active lock holder
        if (m_referenceMode == ReferenceMode::LiveReference && m_arbiter && m_arbiter->activeStationId() == stationId) {
            if (m_refWebClient) {
                m_refWebClient->sendTune(freq, sr, pol);
            }
            if (m_refSerial && m_refSerial->isConnected()) {
                m_refSerial->sendTune(freq, sr, pol);
                m_refSerial->requestStatus();
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
        socket->write(NetMsg::serialize(ack));
    }
}

void TcpServer::onSilenceWatchdogTick() {
    const QDateTime now = QDateTime::currentDateTime();
    QList<QTcpSocket*> staleSockets;
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        QTcpSocket *sock = it.key();
        const ClientSession &session = it.value();
        if (sock && session.status.lastHeartbeat.isValid() && session.status.lastHeartbeat.secsTo(now) > 10) {
            staleSockets.append(sock);
        }
    }
    for (QTcpSocket *sock : staleSockets) {
        emit logMessage(QString("Station %1 timed out (no heartbeat > 10s); disconnecting.")
                            .arg(m_clients.value(sock).stationId));
        sock->abort();
    }
}

void TcpServer::onClientDisconnected() {
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !m_clients.contains(socket)) return;

    auto session = m_clients.take(socket);
    if (session.stationId > 0) {
        if (m_arbiter) {
            m_arbiter->releaseLock(session.stationId);
        }
        emit stationDisconnected(session.stationId);
    }
    socket->deleteLater();
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
    if (m_refWebClient && m_refWebClient->isReachable()) {
        m_refWebClient->requestStatus();
    }
    if (m_refSerial && m_refSerial->isConnected()) {
        m_refSerial->requestStatus();
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
    QTcpSocket *sock = findSocketByStationId(stationId);
    if (sock && sock->isOpen()) {
        sock->write(NetMsg::serialize(msg));
    }
}

QTcpSocket* TcpServer::findSocketByStationId(int stationId) {
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().stationId == stationId) {
            return it.key();
        }
    }
    return nullptr;
}
