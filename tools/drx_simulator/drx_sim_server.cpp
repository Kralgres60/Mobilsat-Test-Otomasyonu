/**
 * @file drx_sim_server.cpp
 * @brief Implementation of the TCP/HTTP-CGI virtual NETA DRX9000 receiver server.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "drx_sim_server.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>

DrxSimServer::DrxSimServer(const QString &deviceId,
                           const QString &macAddr,
                           QObject *parent)
    : QObject(parent),
      m_deviceId(deviceId),
      m_macAddr(macAddr) {
    connect(&m_server, &QTcpServer::newConnection, this, &DrxSimServer::onNewConnection);
}

DrxSimServer::~DrxSimServer() {
    stop();
}

bool DrxSimServer::start(const QString &bindIp, quint16 port) {
    stop();
    m_bindIp = bindIp.trimmed().isEmpty() ? QStringLiteral("0.0.0.0") : bindIp.trimmed();
    m_port = port;

    QHostAddress addr = (m_bindIp == "0.0.0.0")
                            ? QHostAddress::AnyIPv4
                            : QHostAddress(m_bindIp);

    if (!m_server.listen(addr, m_port)) {
        emit logMessage(QString("[HATA] TCP %1:%2 dinlenemedi: %3")
                            .arg(m_bindIp).arg(m_port).arg(m_server.errorString()));
        return false;
    }

    emit logMessage(QString("[TCP AKTİF] Dinlenen port: %1:%2 (http://<Bu_PC_IP>:%2/cgi-bin/drx.cgi)")
                        .arg(m_bindIp).arg(m_port));
    return true;
}

void DrxSimServer::stop() {
    for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
        if (it.key()) {
            it.key()->abort();
            it.key()->deleteLater();
        }
    }
    m_buffers.clear();
    if (m_server.isListening()) {
        m_server.close();
        emit logMessage(QStringLiteral("[TCP DURDURULDU] Port kapatıldı."));
    }
}

void DrxSimServer::setLiveSample(const SimTelemetryPoint &sample) {
    m_liveSample = sample;
}

void DrxSimServer::expireSessionKey() {
    m_forceAccessDenied = true;
    emit logMessage(QStringLiteral("[AUTH] SessionKey geçersiz kılındı; sonraki istekte access=denied dönecek."));
}

QString DrxSimServer::cleanPeerAddress(const QTcpSocket *socket) {
    if (!socket) return QStringLiteral("-");
    QHostAddress addr = socket->peerAddress();
    bool ok = false;
    quint32 ipv4 = addr.toIPv4Address(&ok);
    if (ok) return QHostAddress(ipv4).toString();
    return addr.toString();
}

void DrxSimServer::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QTcpSocket *sock = m_server.nextPendingConnection();
        m_buffers.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead, this, &DrxSimServer::onSocketReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &DrxSimServer::onSocketDisconnected);
    }
}

void DrxSimServer::onSocketDisconnected() {
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    m_buffers.remove(sock);
    sock->deleteLater();
}

void DrxSimServer::onSocketReadyRead() {
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock || !m_buffers.contains(sock)) return;

    m_buffers[sock].append(sock->readAll());
    QByteArray &buf = m_buffers[sock];

    int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;

    QByteArray headersPart = buf.left(headerEnd);
    int contentLength = 0;
    const QList<QByteArray> headerLines = headersPart.split('\n');
    for (const QByteArray &line : headerLines) {
        QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith("content-length:")) {
            contentLength = trimmed.mid(15).trimmed().toInt();
        }
    }

    int totalNeeded = headerEnd + 4 + contentLength;
    if (buf.size() < totalNeeded) return;

    QByteArray fullReq = buf.left(totalNeeded);
    buf.remove(0, totalNeeded);
    handleHttpRequest(sock, fullReq);
}

void DrxSimServer::handleHttpRequest(QTcpSocket *socket, const QByteArray &rawRequest) {
    const QString peerIp = cleanPeerAddress(socket);
    const int headerEnd = rawRequest.indexOf("\r\n\r\n");
    const QByteArray headerBlock = (headerEnd >= 0) ? rawRequest.left(headerEnd) : rawRequest;
    const QByteArray body = (headerEnd >= 0) ? rawRequest.mid(headerEnd + 4) : QByteArray();

    const QString firstLine = QString::fromUtf8(headerBlock.split('\n').value(0)).trimmed();

    // Handle quick browser GET / health-check directly
    if (firstLine.startsWith("GET ", Qt::CaseInsensitive)) {
        m_requestCount++;
        QJsonObject summary;
        summary["simulator"] = "NETA DRX9000 TCP Virtual Receiver";
        summary["deviceId"] = m_deviceId;
        summary["tunedFreqMhz"] = m_currentFreqMhz;
        summary["symbolRate"] = m_currentSymbolRate;
        summary["polarity"] = m_currentPol;
        summary["cnDb"] = m_liveSample.cnDb;
        summary["powerDbm"] = m_liveSample.powerDbm;
        summary["signalLock"] = m_liveSample.signalLock;
        summary["videoLock"] = m_liveSample.videoLock;
        summary["audioLock"] = m_liveSample.audioLock;
        summary["note"] = m_liveSample.note;

        emit requestHandled(m_requestCount, peerIp, "GET_HEALTH");
        socket->write(buildJsonResponse(summary));
        socket->disconnectFromHost();
        return;
    }

    // Parse newline-separated key=value pairs from CGI body
    QMap<QString, QString> params;
    const QStringList lines = QString::fromUtf8(body).split('\n', Qt::SkipEmptyParts);
    for (const QString &l : lines) {
        const int eq = l.indexOf('=');
        if (eq > 0) {
            params.insert(l.left(eq).trimmed(), l.mid(eq + 1).trimmed());
        }
    }

    m_requestCount++;
    const QString responseTo = params.value("ResponseTo");
    const QString query      = params.value("query");
    const QString reqSession = params.value("sessionKey");

    QJsonObject jsonResp;
    QString actionTag = "STATUS";

    if (responseTo == "login") {
        actionTag = "LOGIN";
        const QString user = params.value("lg_username");
        const QString pass = params.value("lg_password");
        if (user == "admin" && pass == "1234") {
            m_forceAccessDenied = false;
            m_activeSessionKey = 100000 + QRandomGenerator::global()->bounded(899999);
            jsonResp["sessionkey"] = m_activeSessionKey;
            emit logMessage(QString("[%1] LOGIN OK -> sessionkey=%2").arg(peerIp).arg(m_activeSessionKey));
        } else {
            jsonResp["access"] = "denied";
            emit logMessage(QString("[%1] LOGIN HATALI (%2)").arg(peerIp, user));
        }
    } else if (m_forceAccessDenied || reqSession.toInt() != m_activeSessionKey) {
        actionTag = "DENIED";
        jsonResp["access"] = "denied";
        emit logMessage(QString("[%1] Geçersiz sessionKey (%2) -> access=denied").arg(peerIp, reqSession));
    } else if (responseTo == "freq") {
        actionTag = "TUNE";
        const int freqKhz = params.value("fr_frequency").toInt();
        const int srBd    = params.value("fr_srate").toInt();
        const int polEnum = params.value("fr_pol").toInt();

        m_currentFreqMhz    = (freqKhz >= 50000) ? (freqKhz / 1000) : freqKhz;
        m_currentSymbolRate = (srBd >= 100000) ? (srBd / 1000) : srBd;
        m_currentPol        = (polEnum == 1) ? "H" : "V";

        m_isTuning = true;
        QTimer::singleShot(250, this, [this]() { m_isTuning = false; });

        jsonResp["status"] = "ok";
        jsonResp["fr_frequency"] = freqKhz;
        emit tuned(m_currentFreqMhz, m_currentSymbolRate, m_currentPol);
        emit logMessage(QString("[%1] TUNE -> %2 MHz | %3 kS/s | %4")
                            .arg(peerIp).arg(m_currentFreqMhz).arg(m_currentSymbolRate).arg(m_currentPol));
    } else if (query == "deviceInfo") {
        actionTag = "DEVICE_INFO";
        QJsonArray elems;
        auto addEl = [&](const QString &dom, const QString &cap, const QString &val) {
            QJsonObject o;
            o["domid"] = dom;
            o["caption"] = cap;
            o["value"] = val;
            elems.append(o);
        };
        addEl("di_sw", "SW Versiyon", "v4.0-GRAPH-SIM");
        addEl("di_devid", "Cihaz ID", m_deviceId);
        addEl("di_mac", "MAC Adresi", m_macAddr);
        addEl("di_tuner", "Tuner Model", "DVB-S2X-SIM");

        QJsonObject sec;
        sec["elements"] = elems;
        QJsonArray secs;
        secs.append(sec);
        jsonResp["sections"] = secs;
    } else {
        actionTag = "STATUS";
        jsonResp = buildAlarmStatusJson();
    }

    emit requestHandled(m_requestCount, peerIp, actionTag);
    socket->write(buildJsonResponse(jsonResp));
    socket->disconnectFromHost();
}

QJsonObject DrxSimServer::buildAlarmStatusJson() const {
    const bool sigLock = !m_isTuning && m_liveSample.signalLock;
    const bool vidLock = sigLock && m_liveSample.videoLock;
    const bool audLock = sigLock && m_liveSample.audioLock;

    double cnVal  = m_isTuning ? 0.0 : m_liveSample.cnDb;
    double powVal = m_isTuning ? -85.0 : m_liveSample.powerDbm;

    if (m_noiseEnabled && sigLock) {
        const double jitter = (QRandomGenerator::global()->bounded(31) - 15) / 100.0;
        cnVal  = qMax(0.0, cnVal + jitter);
        powVal = powVal + jitter;
    }

    QJsonArray elems;
    auto addEl = [&](const QString &dom, const QString &cap, const QString &val) {
        QJsonObject o;
        o["domid"] = dom;
        o["caption"] = cap;
        o["value"] = val;
        elems.append(o);
    };

    addEl("st_current_time", "Saat", QDateTime::currentDateTime().toString("hh:mm:ss"));
    addEl("st_channel", "Kanal", resolveChannelName(m_currentFreqMhz, m_currentPol));
    addEl("st_signal", "Sinyal", sigLock ? "tick" : "cross");
    addEl("st_video", "Video", vidLock ? "tick" : "cross");
    addEl("st_audio", "Ses", audLock ? "tick" : "cross");
    addEl("st_cn", "C/N", QString("%1 dB").arg(cnVal, 0, 'f', 2));
    addEl("st_power", "Güç", QString("%1 dBm").arg(powVal, 0, 'f', 2));
    addEl("st_ber", "BER", sigLock ? m_liveSample.ber : QStringLiteral("1.0E-1"));
    addEl("st_ebn0", "Eb/N0", QString("%1 dB").arg(qMax(0.0, cnVal - 2.8), 0, 'f', 2));

    QJsonObject sec;
    sec["elements"] = elems;
    QJsonArray secs;
    secs.append(sec);

    QJsonObject root;
    root["sections"] = secs;
    return root;
}

QByteArray DrxSimServer::buildJsonResponse(const QJsonObject &obj,
                                           int httpCode,
                                           const QString &statusText) const {
    const QByteArray jsonBytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray http;
    http.append(QString("HTTP/1.1 %1 %2\r\n").arg(httpCode).arg(statusText).toUtf8());
    http.append("Content-Type: application/json; charset=utf-8\r\n");
    http.append("Content-Length: " + QByteArray::number(jsonBytes.size()) + "\r\n");
    http.append("Connection: close\r\n\r\n");
    http.append(jsonBytes);
    return http;
}

QString DrxSimServer::resolveChannelName(int freqMhz, const QString &pol) const {
    if (freqMhz == 11977) return QStringLiteral("TRT 1 HD (%1)").arg(pol);
    if (freqMhz == 11054) return QStringLiteral("TRT HABER HD (%1)").arg(pol);
    if (freqMhz == 12015) return QStringLiteral("ATV HD (%1)").arg(pol);
    if (freqMhz == 12209) return QStringLiteral("SHOW TV HD (%1)").arg(pol);
    if (freqMhz == 12245) return QStringLiteral("STAR TV HD (%1)").arg(pol);
    return QString("CH_%1MHz_%2").arg(freqMhz).arg(pol);
}
