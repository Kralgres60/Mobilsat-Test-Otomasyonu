/**
 * @file data_transfer.cpp
 * @brief Donanım veri iletimi (DataTransfer) temel sınıfı ve ondan türeyen
 *        Ethernet (Web CGI) ile RS232 (Seri Port) sürücülerinin birleşik uygulaması.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "data_transfer.h"
#include <QProcess>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QDebug>

// =============================================================================
// 1. ETHERNET SÜRÜCÜSÜ (WEB CGI + ICMP PING)
// =============================================================================
Ethernet::Ethernet(QObject *parent)
    : DataTransfer(parent) {
    connect(&m_netManager, &QNetworkAccessManager::finished,
            this, &Ethernet::onReplyFinished);
}

Ethernet::Ethernet(const QString &hostIp, int port, QObject *parent)
    : DataTransfer(parent), m_hostIp(hostIp), m_port(port) {
    connect(&m_netManager, &QNetworkAccessManager::finished,
            this, &Ethernet::onReplyFinished);
}

Ethernet::~Ethernet() {
    resetSession();
}

void Ethernet::setHost(const QString &hostIp, int port) {
    if (m_hostIp != hostIp || m_port != port) {
        m_hostIp = hostIp.trimmed();
        m_port = (port > 0) ? port : 80;
        resetSession();
    }
}

void Ethernet::setCredentials(const QString &username, const QString &password) {
    m_username = username;
    m_password = password;
}

void Ethernet::resetSession() {
    m_sessionKey = 0;
    m_isReachable = false;
    m_lastLatencyMs = -1;
    m_pendingActionAfterLogin.clear();
    m_pendingDataAfterLogin.clear();
}

bool Ethernet::connectDevice() {
    testConnectionAndSync();
    return true;
}

void Ethernet::disconnectDevice() {
    resetSession();
    emit deviceDisconnected();
}

bool Ethernet::isConnected() const {
    return m_isReachable;
}

bool Ethernet::isDeviceReady() const {
    return m_isReachable && (m_sessionKey > 0);
}

void Ethernet::pingHost(int timeoutMs) {
    if (m_hostIp.isEmpty() || m_pingInProgress) return;
    m_pingInProgress = true;

    QString program;
    QStringList args;

#if defined(Q_OS_WIN)
    program = "ping";
    args << "-n" << "1" << "-w" << QString::number(timeoutMs) << m_hostIp;
#else
    program = "ping";
    int timeoutSec = qMax(1, timeoutMs / 1000);
    args << "-c" << "1" << "-W" << QString::number(timeoutSec) << m_hostIp;
#endif

    QProcess *proc = new QProcess(this);
    QElapsedTimer *timer = new QElapsedTimer();
    timer->start();

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, timer](int exitCode, QProcess::ExitStatus status) {
        int elapsed = static_cast<int>(timer->elapsed());
        bool reachable = (status == QProcess::NormalExit && exitCode == 0);
        m_isReachable = reachable;
        m_lastLatencyMs = reachable ? elapsed : -1;
        m_pingInProgress = false;

        delete timer;
        proc->deleteLater();

        emit pingFinished(reachable, m_lastLatencyMs, m_hostIp);
        if (!reachable) {
            emit deviceDisconnected();
            emit logMessage(QString("[Ping] DRX alıcıya ulaşılamıyor: %1 (%2 ms)").arg(m_hostIp).arg(elapsed), true);
        }

        if (m_syncAfterPing) {
            m_syncAfterPing = false;
            if (reachable) {
                login();
            } else {
                emit loginFailed(QString("Host ping yanıtı vermiyor (%1)").arg(m_hostIp));
            }
        }
    });

    proc->start(program, args);
}

void Ethernet::testConnectionAndSync() {
    m_syncAfterPing = true;
    pingHost(1500);
}

QUrl Ethernet::cgiUrl() const {
    QUrl url;
    url.setScheme("http");
    url.setHost(m_hostIp);
    url.setPort(m_port);
    url.setPath("/cgi-bin/drx.cgi");
    return url;
}

void Ethernet::login() {
    QJsonObject obj;
    obj["ResponseTo"] = "login";
    obj["username"] = m_username;
    obj["password"] = m_password;

    QJsonDocument doc(obj);
    sendCgiRequest("login", doc.toJson(QJsonDocument::Compact));
}

void Ethernet::requestStatus() {
    QJsonObject obj;
    obj["query"] = "alarmStatus";
    QJsonDocument doc(obj);
    sendCgiRequest("alarmStatus", doc.toJson(QJsonDocument::Compact));
}

void Ethernet::requestDeviceInfo() {
    QJsonObject obj;
    obj["query"] = "deviceInfo";
    QJsonDocument doc(obj);
    sendCgiRequest("deviceInfo", doc.toJson(QJsonDocument::Compact));
}

void Ethernet::sendTune(int freqMHz, int symbolRate, const QString &polarity) {
    QJsonObject obj;
    obj["ResponseTo"] = "freq";
    obj["frequency"] = freqMHz * 1000;
    obj["symbolRate"] = symbolRate * 1000;
    obj["polarity"] = polarity.toUpper();
    obj["lnb"] = 0;

    QJsonDocument doc(obj);
    sendCgiRequest("freq", doc.toJson(QJsonDocument::Compact));
}

void Ethernet::sendRaw(const QString &command) {
    QString q = command.trimmed();
    QJsonObject obj;
    if (q.startsWith("{") && q.endsWith("}")) {
        QJsonDocument parsed = QJsonDocument::fromJson(q.toUtf8());
        if (parsed.isObject()) obj = parsed.object();
        else obj["query"] = q;
    } else {
        obj["query"] = q;
    }
    QJsonDocument doc(obj);
    sendCgiRequest("raw", doc.toJson(QJsonDocument::Compact));
}

void Ethernet::sendCgiRequest(const QString &actionType, const QByteArray &postData) {
    if (actionType != "login" && m_sessionKey <= 0) {
        m_pendingActionAfterLogin = actionType;
        m_pendingDataAfterLogin = postData;
        login();
        return;
    }

    QByteArray finalPayload = postData;
    if (m_sessionKey > 0 && actionType != "login") {
        QJsonDocument doc = QJsonDocument::fromJson(postData);
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            root["sessionkey"] = m_sessionKey;
            finalPayload = QJsonDocument(root).toJson(QJsonDocument::Compact);
        }
    }

    QNetworkRequest request(cgiUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::Attribute(QNetworkRequest::User + 1), actionType);

    QNetworkReply *reply = m_netManager.post(request, finalPayload);
    QTimer *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    timeoutTimer->start(m_timeoutMs);
}

void Ethernet::onReplyFinished(QNetworkReply *reply) {
    QString actionType = reply->request().attribute(QNetworkRequest::Attribute(QNetworkRequest::User + 1)).toString();
    QByteArray payload = reply->readAll();
    QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        QString errStr = reply->errorString();
        emit logMessage(QString("[CGI Hata %1] %2").arg(actionType, errStr), true);
        if (actionType == "login") {
            emit loginFailed(errStr);
        }
        return;
    }

    parseCgiResponse(actionType, payload);
}

void Ethernet::parseCgiResponse(const QString &actionType, const QByteArray &payload) {
    emit rawResponseReceived(actionType, payload);
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();
    if (actionType == "login") {
        handleLoginResponse(root);
    } else if (actionType == "alarmStatus") {
        handleAlarmStatusResponse(root);
    } else if (actionType == "deviceInfo") {
        handleDeviceInfoResponse(root);
    } else if (actionType == "freq") {
        handleTuneResponse(root);
    }
}

void Ethernet::handleLoginResponse(const QJsonObject &root) {
    int key = root.value("sessionkey").toInt(-1);
    if (key <= 0 && root.contains("key")) key = root.value("key").toInt(-1);

    if (key > 0) {
        m_sessionKey = key;
        emit loginSucceeded(key);
        emit deviceConnected(QString("%1:%2").arg(m_hostIp).arg(m_port));
        emit logMessage(QString("[CGI] Giriş başarılı (SessionKey: %1)").arg(key), false);

        if (!m_pendingActionAfterLogin.isEmpty()) {
            QString nextAction = m_pendingActionAfterLogin;
            QByteArray nextData = m_pendingDataAfterLogin;
            m_pendingActionAfterLogin.clear();
            m_pendingDataAfterLogin.clear();
            sendCgiRequest(nextAction, nextData);
        }
    } else {
        QString reason = root.value("reason").toString("Geçersiz kullanıcı adı / şifre");
        emit loginFailed(reason);
        emit logMessage(QString("[CGI Giriş Başarısız] %1").arg(reason), true);
    }
}

void Ethernet::handleAlarmStatusResponse(const QJsonObject &root) {
    bool sig = false;
    bool vid = false;
    bool aud = false;
    QString chName;

    if (root.contains("signalLock")) sig = root.value("signalLock").toBool(false);
    else if (root.contains("sLock")) sig = (root.value("sLock").toInt(0) == 1);

    if (root.contains("videoLock")) vid = root.value("videoLock").toBool(false);
    else if (root.contains("vLock")) vid = (root.value("vLock").toInt(0) == 1);

    if (root.contains("audioLock")) aud = root.value("audioLock").toBool(false);
    else if (root.contains("aLock")) aud = (root.value("aLock").toInt(0) == 1);

    chName = root.value("channelName").toString();

    emit statusReceived(sig, vid, aud, chName);
    emit statusReceivedFull(sig, vid, aud, chName);

    double pwr = root.value("powerDbm").toDouble(root.value("pow").toDouble(-99.0));
    double cn = root.value("cnDb").toDouble(root.value("cn").toDouble(0.0));
    double ber = root.value("ber").toDouble(1.0e-7);
    double ebn0 = root.value("ebN0Db").toDouble(root.value("ebn0").toDouble(0.0));

    emit telemetryReceived(pwr, cn, ber, ebn0);
    emit telemetryReceivedFull(pwr, cn, ber, ebn0);
}

void Ethernet::handleDeviceInfoResponse(const QJsonObject &root) {
    QString sw = root.value("swVersion").toString(root.value("sw").toString());
    QString devId = root.value("deviceId").toString(root.value("id").toString());
    QString mac = root.value("mac").toString();
    QString tuner = root.value("tunerModel").toString(root.value("tuner").toString());

    emit deviceInfoReceived("sw_version", sw);
    emit deviceInfoReceived("device_id", devId);
    emit deviceInfoReceived("mac", mac);
    emit deviceInfoReceived("tuner_model", tuner);
    emit deviceInfoReceivedFull(sw, devId, mac, tuner);
}

void Ethernet::handleTuneResponse(const QJsonObject &root) {
    bool ok = root.value("status").toString().toLower() == "ok" || root.value("result").toBool(true);
    QString details = root.value("message").toString("Tune komutu işlendi");
    emit tuneCompleted(ok, details);
}

// =============================================================================
// 2. RS232 SÜRÜCÜSÜ (SERİ PORT + DAHİLİ PROTOKOL PARSER)
// =============================================================================
RS232::RS232(QObject *parent)
    : DataTransfer(parent) {
    connect(&m_serial, &QSerialPort::readyRead, this, &RS232::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &RS232::onErrorOccurred);
}

RS232::RS232(const QString &portName, int baudRate, QObject *parent)
    : DataTransfer(parent), m_portName(portName), m_baudRate(baudRate) {
    connect(&m_serial, &QSerialPort::readyRead, this, &RS232::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &RS232::onErrorOccurred);
}

RS232::~RS232() {
    closePort();
}

QString RS232::readyStateString() const {
    switch (m_readyState) {
        case DeviceReadyState::Disconnected: return "Bağlantı Yok";
        case DeviceReadyState::PortOpenWaitingDevice: return "Cihaz Bekleniyor";
        case DeviceReadyState::Booting: return "Açılıyor (Booting)";
        case DeviceReadyState::Ready: return "Hazır (Ready)";
    }
    return "Bilinmiyor";
}

void RS232::setReadyState(DeviceReadyState newState, const QString &desc) {
    if (m_readyState != newState) {
        m_readyState = newState;
        emit deviceReadyStateChanged(newState, desc);
        emit logMessage(QString("[RS232 Durum] %1 (%2)").arg(readyStateString(), desc), false);
    }
}

QStringList RS232::availablePorts() {
    QStringList ports;
    const auto list = QSerialPortInfo::availablePorts();
    for (const auto &info : list) {
        ports << info.portName();
    }
    return ports;
}

bool RS232::connectDevice() {
    return openPort(m_portName, m_baudRate);
}

void RS232::disconnectDevice() {
    closePort();
}

bool RS232::isConnected() const {
    return m_serial.isOpen();
}

bool RS232::openPort(const QString &portName, int baudRate) {
    if (m_serial.isOpen()) {
        m_serial.close();
    }

    m_portName = portName;
    m_baudRate = baudRate;

    m_serial.setPortName(portName);
    m_serial.setBaudRate(baudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (m_serial.open(QIODevice::ReadWrite)) {
        m_rxBuffer.clear();
        setReadyState(DeviceReadyState::PortOpenWaitingDevice, "Port açıldı, cihaz bekleniyor...");
        emit portOpened(portName);
        emit deviceConnected(portName);
        emit logMessage(QString("Seri Port Açıldı: %1 (%2 baud)").arg(portName).arg(baudRate), false);

        sendRaw("getstatus\n");
        return true;
    } else {
        QString err = m_serial.errorString();
        setReadyState(DeviceReadyState::Disconnected, err);
        emit portError(err);
        emit logMessage(QString("Seri port açılamadı (%1): %2").arg(portName, err), true);
        return false;
    }
}

void RS232::closePort() {
    if (m_serial.isOpen()) {
        m_serial.close();
        setReadyState(DeviceReadyState::Disconnected, "Port kapatıldı");
        emit portClosed();
        emit deviceDisconnected();
        emit logMessage("Seri port kapatıldı.", false);
    }
}

void RS232::setPortConfig(const QString &portName, int baudRate) {
    m_portName = portName;
    m_baudRate = baudRate;
}

void RS232::sendTune(int freqMhz, int symbolRate, const QString &polarity) {
    if (!isConnected()) return;
    QString cmd = QString("tune %1 %2 %3\n").arg(freqMhz).arg(symbolRate).arg(polarity.toLower());
    sendRaw(cmd);
}

void RS232::requestStatus() {
    if (!isConnected()) return;
    sendRaw("getstatus\n");
}

void RS232::requestDeviceInfo() {
    if (!isConnected()) return;
    sendRaw("getstatus\n");
}

void RS232::startTunerLog() {
    if (!isConnected()) return;
    sendRaw("tunerlog on\n");
}

void RS232::stopTunerLog() {
    if (!isConnected()) return;
    sendRaw("tunerlog off\n");
}

void RS232::sendRaw(const QString &command) {
    if (!isConnected()) return;
    QByteArray data = command.toUtf8();
    m_serial.write(data);
    emit logMessage(command.trimmed(), true);
}

void RS232::onReadyRead() {
    m_rxBuffer.append(m_serial.readAll());
    processRxBuffer();
}

void RS232::onErrorOccurred(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::ResourceError || error == QSerialPort::PermissionError) {
        emit portError(m_serial.errorString());
        closePort();
    }
}

void RS232::processRxBuffer() {
    static const QRegularExpression ansiRegex("\x1b\\[[0-9;]*[a-zA-Z]");
    m_rxBuffer.replace('\0', ' ');

    while (m_rxBuffer.contains('\n') || m_rxBuffer.contains('\r')) {
        int indexLf = m_rxBuffer.indexOf('\n');
        int indexCr = m_rxBuffer.indexOf('\r');
        int index = (indexLf != -1 && indexCr != -1) ? qMin(indexLf, indexCr) : (indexLf != -1 ? indexLf : indexCr);

        QByteArray lineBytes = m_rxBuffer.left(index);
        m_rxBuffer.remove(0, index + 1);

        QString line = QString::fromUtf8(lineBytes.constData(), lineBytes.size()).remove(ansiRegex).trimmed();
        if (line.isEmpty()) continue;

        emit logMessage(line, false);

        if (line.contains("-->") || line.contains("Select Option:") || line.endsWith(":")) {
            emit devicePromptDetected(line);
            setReadyState(DeviceReadyState::Ready, "Komut istemi tespit edildi (-->)");
        }

        if (line.contains("getstatus:") || line.contains("status:")) {
            bool s = line.contains("S:+");
            bool v = line.contains("V:+");
            bool a = line.contains("A:+");
            setReadyState(DeviceReadyState::Ready, "Durum yanıtı alındı");
            emit statusReceived(s, v, a);
        } else if (line.contains("POW:", Qt::CaseInsensitive) || line.contains("C/N:", Qt::CaseInsensitive)) {
            double pow = 0, cn = 0, ber = 1.0e-7;
            if (parseRfTelemetryLine(line, pow, cn, ber)) {
                emit telemetryReceived(pow, cn, ber);
            }
        } else if (line.contains("SW Ver", Qt::CaseInsensitive)) {
            QString val = extractValueAfterKey(line, "SW Ver");
            if (!val.isEmpty()) {
                m_swVer = val;
                emit deviceInfoReceived("sw_version", val);
            }
        } else if (line.contains("HW Ver", Qt::CaseInsensitive)) {
            QString val = extractValueAfterKey(line, "HW Ver");
            if (!val.isEmpty()) {
                m_hwVer = val;
                emit deviceInfoReceived("hw_version", val);
            }
        } else if (line.contains("NETA DRX", Qt::CaseInsensitive)) {
            m_model = line;
            emit deviceInfoReceived("model", line);
        }
    }
}

bool RS232::parseRfTelemetryLine(const QString &line, double &outPow, double &outCn, double &outBer) {
    if (line.contains("POW:", Qt::CaseInsensitive) && line.contains("C/N:", Qt::CaseInsensitive)) {
        outPow = extractValueAfterKey(line, "POW:").remove("dBm", Qt::CaseInsensitive).toDouble();
        outCn = extractValueAfterKey(line, "C/N:").remove("dB", Qt::CaseInsensitive).toDouble();
        outBer = extractValueAfterKey(line, "BER:").toDouble();
        if (outBer <= 0) outBer = 1.0e-7;
        return (outCn != 0 || outPow != 0);
    }
    return false;
}

QString RS232::extractValueAfterKey(const QString &line, const QString &key) {
    int keyPos = line.indexOf(key, 0, Qt::CaseInsensitive);
    if (keyPos == -1) return QString();

    int startPos = keyPos + key.length();
    while (startPos < line.length() && (line[startPos] == ':' || line[startPos] == ' ' || line[startPos] == '\t')) {
        startPos++;
    }
    if (startPos >= line.length()) return QString();

    QString val = line.mid(startPos).trimmed();
    int endPos = val.indexOf(QRegularExpression("[\\s,\\;]"));
    if (endPos != -1) val = val.left(endPos).trimmed();
    return val;
}
