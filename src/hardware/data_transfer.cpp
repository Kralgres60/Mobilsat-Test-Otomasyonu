/**
 * @file data_transfer.cpp
 * @brief Implementation of DataTransfer base class and Ethernet (Web CGI) and RS232 drivers.
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
// HELPER NUMERIC AND LOCK PARSERS (DRX DOM & JSON)
// =============================================================================
static double parseDrxNumeric(QString str) {
    str = str.trimmed();
    if (str.isEmpty() || str == "?" || str.compare("nan", Qt::CaseInsensitive) == 0) {
        return 0.0;
    }
    static QRegularExpression signSpaceRegex(R"(([-+])\s+)");
    str.replace(signSpaceRegex, R"(\1)");
    str.replace(',', '.');
    static QRegularExpression numRegex(R"([-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?)");
    QRegularExpressionMatch m = numRegex.match(str);
    if (m.hasMatch()) {
        bool ok = false;
        double val = m.captured(0).toDouble(&ok);
        if (ok) return val;
    }
    return 0.0;
}

static bool isDrxLockPositive(QString str) {
    str = str.trimmed().toLower();
    if (str.isEmpty() || str == "cross" || str == "?" || str == "0" || str == "false" || str == "no" || str == "unlocked") {
        return false;
    }
    return (str == "tick" || str.contains("tick") || str == "ok" || str == "true" || str == "1" || str.contains("lock") || str == "yes");
}

static QJsonArray extractElements(const QJsonObject &root) {
    QJsonArray allElements;
    if (root.contains("sections")) {
        const QJsonArray sections = root.value("sections").toArray();
        for (const auto &secVal : sections) {
            const QJsonArray subElems = secVal.toObject().value("elements").toArray();
            for (const auto &e : subElems) {
                allElements.append(e);
            }
        }
    }
    if (root.contains("elements")) {
        const QJsonArray rootElems = root.value("elements").toArray();
        for (const auto &e : rootElems) {
            allElements.append(e);
        }
    }
    return allElements;
}

// =============================================================================
// 1. ETHERNET DRIVER (WEB CGI + ICMP PING)
// =============================================================================
Ethernet::Ethernet(QObject *parent)
    : DataTransfer(parent) {
    connect(&m_netManager, &QNetworkAccessManager::finished,
            this, &Ethernet::onReplyFinished);
}

Ethernet::Ethernet(const QString &hostIp, int port, QObject *parent)
    : DataTransfer(parent), m_hostIp(hostIp.trimmed()), m_port(port) {
    connect(&m_netManager, &QNetworkAccessManager::finished,
            this, &Ethernet::onReplyFinished);
}

Ethernet::~Ethernet() {
    resetSession();
}

void Ethernet::setHost(const QString &hostIp, int port) {
    QString cleanHost = hostIp.trimmed();
    if (cleanHost.startsWith("http://", Qt::CaseInsensitive)) cleanHost = cleanHost.mid(7);
    else if (cleanHost.startsWith("https://", Qt::CaseInsensitive)) cleanHost = cleanHost.mid(8);

    int newPort = (port > 0) ? port : 80;
    QString newIp = cleanHost;
    if (cleanHost.contains(':')) {
        QStringList parts = cleanHost.split(':');
        newIp = parts[0].trimmed();
        bool ok = false;
        int p = parts[1].trimmed().toInt(&ok);
        if (ok && p > 0) newPort = p;
    }

    if (newIp != m_hostIp || newPort != m_port) {
        m_hostIp = newIp;
        m_port = newPort;
        m_isReachable = false;
        m_lastLatencyMs = -1;
        resetSession();
    }
}

void Ethernet::setCredentials(const QString &username, const QString &password) {
    m_username = username.trimmed();
    m_password = password;
}

void Ethernet::resetSession() {
    m_sessionKey = 0;
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
    if (m_hostIp.isEmpty()) {
        m_isReachable = false;
        m_lastLatencyMs = -1;
        emit pingFinished(false, -1, m_hostIp);
        return;
    }
    if (m_pingInProgress) return;

    // For local simulator (127.0.0.1 / localhost), ping is always instant and successful
    if (m_hostIp == "127.0.0.1" || m_hostIp.compare("localhost", Qt::CaseInsensitive) == 0) {
        m_isReachable = true;
        m_lastLatencyMs = 1;
        emit pingFinished(true, 1, m_hostIp);
        if (m_syncAfterPing) {
            m_syncAfterPing = false;
            login();
        }
        return;
    }

    m_pingInProgress = true;
    QString targetIp = m_hostIp;

    QString program;
    QStringList args;

#if defined(Q_OS_WIN)
    program = "ping";
    args << "-n" << "1" << "-w" << QString::number(timeoutMs) << targetIp;
#else
    program = "ping";
    int timeoutSec = qMax(1, timeoutMs / 1000);
    args << "-c" << "1" << "-W" << QString::number(timeoutSec) << targetIp;
#endif

    QProcess *proc = new QProcess(this);
    QElapsedTimer *timer = new QElapsedTimer();
    timer->start();

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, timer, targetIp](int exitCode, QProcess::ExitStatus status) {
        int elapsed = static_cast<int>(timer->elapsed());
        QString output = QString::fromLocal8Bit(proc->readAllStandardOutput());
        bool hasTtl = output.contains("TTL=", Qt::CaseInsensitive);
        bool reachable = (status == QProcess::NormalExit && exitCode == 0 && (hasTtl || output.contains("ttl=")));

        m_isReachable = reachable;
        m_lastLatencyMs = reachable ? elapsed : -1;
        m_pingInProgress = false;

        delete timer;
        proc->deleteLater();

        emit pingFinished(reachable, m_lastLatencyMs, targetIp);
        if (!reachable) {
            emit deviceDisconnected();
            emit logMessage(QString("[Ping] DRX alıcıya ulaşılamıyor: %1 (%2 ms)").arg(targetIp).arg(elapsed), true);
        }

        if (m_syncAfterPing) {
            m_syncAfterPing = false;
            if (reachable) {
                login();
            } else {
                emit loginFailed(QString("Host ping yanıtı vermiyor (%1)").arg(targetIp));
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
    QByteArray postData;
    postData.append("ResponseTo=login\n");
    postData.append("sessionKey=\n");
    postData.append("lg_username=" + m_username.toUtf8() + "\n");
    postData.append("lg_password=" + m_password.toUtf8() + "\n");

    emit logMessage(QString("[CGI Giriş] %1@%2:%3").arg(m_username, m_hostIp).arg(m_port), false);
    sendCgiRequest("login", postData);
}

void Ethernet::requestStatus() {
    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "alarmStatus";
        login();
        return;
    }
    QByteArray postData;
    postData.append("query=alarmStatus\n");
    postData.append(QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());
    sendCgiRequest("alarmStatus", postData);
}

void Ethernet::requestDeviceInfo() {
    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "deviceInfo";
        login();
        return;
    }
    QByteArray postData;
    postData.append("query=deviceInfo\n");
    postData.append(QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());
    sendCgiRequest("deviceInfo", postData);
}

void Ethernet::sendTune(int freqMHz, int symbolRate, const QString &polarity) {
    int freqKhz = (freqMHz < 50000) ? (freqMHz * 1000) : freqMHz;
    int srBdps = (symbolRate < 100000) ? (symbolRate * 1000) : symbolRate;
    int polEnum = (polarity.trimmed().toUpper().startsWith('H')) ? 1 : 0;

    QByteArray postData;
    postData.append("ResponseTo=freq\n");
    postData.append(QString("sessionKey=%1\n").arg(m_sessionKey > 0 ? QString::number(m_sessionKey) : "").toUtf8());
    postData.append(QString("fr_frequency=%1\n").arg(freqKhz).toUtf8());
    postData.append(QString("fr_srate=%1\n").arg(srBdps).toUtf8());
    postData.append(QString("fr_pol=%1\n").arg(polEnum).toUtf8());

    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "freq";
        m_pendingDataAfterLogin = postData;
        login();
        return;
    }

    sendCgiRequest("freq", postData);
}

void Ethernet::sendRaw(const QString &command) {
    QString q = command.trimmed();
    QByteArray postData;
    if (q.contains("=")) {
        postData = q.toUtf8();
        if (!postData.endsWith('\n')) postData.append('\n');
    } else {
        postData.append("query=" + q.toUtf8() + "\n");
    }
    if (m_sessionKey > 0 && !postData.contains("sessionKey=")) {
        postData.append(QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());
    }
    sendCgiRequest("raw", postData);
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
        if (finalPayload.contains("sessionKey=\n")) {
            finalPayload.replace("sessionKey=\n", QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());
        }
    }

    QNetworkRequest request(cgiUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::UserAgentHeader, "NETA-DRX9000-Automator");
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
    } else if (actionType == "alarmStatus" || actionType == "STATUS") {
        handleAlarmStatusResponse(root);
    } else if (actionType == "deviceInfo" || actionType == "DEVICE_INFO") {
        handleDeviceInfoResponse(root);
    } else if (actionType == "freq" || actionType == "TUNE") {
        handleTuneResponse(root);
    }
}

void Ethernet::handleLoginResponse(const QJsonObject &root) {
    int key = -1;
    if (root.contains("sessionkey")) {
        key = root.value("sessionkey").toInt(-1);
        if (key <= 0) key = root.value("sessionkey").toString().toInt();
    } else if (root.contains("sessionKey")) {
        key = root.value("sessionKey").toInt(-1);
        if (key <= 0) key = root.value("sessionKey").toString().toInt();
    } else if (root.contains("key")) {
        key = root.value("key").toInt(-1);
    }

    if (key > 0) {
        m_sessionKey = key;
        m_isReachable = true;
        emit loginSucceeded(key);
        emit deviceConnected(QString("%1:%2").arg(m_hostIp).arg(m_port));
        emit logMessage(QString("[CGI] Giriş başarılı (SessionKey: %1)").arg(key), false);

        if (!m_pendingActionAfterLogin.isEmpty()) {
            QString nextAction = m_pendingActionAfterLogin;
            QByteArray nextData = m_pendingDataAfterLogin;
            m_pendingActionAfterLogin.clear();
            m_pendingDataAfterLogin.clear();

            if (nextAction == "alarmStatus" || nextAction == "STATUS") {
                requestStatus();
            } else if (nextAction == "deviceInfo" || nextAction == "DEVICE_INFO") {
                requestDeviceInfo();
            } else if (nextAction == "freq" || nextAction == "TUNE") {
                if (nextData.contains("sessionKey=\n")) {
                    nextData.replace("sessionKey=\n", QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());
                }
                sendCgiRequest("freq", nextData);
            } else {
                sendCgiRequest(nextAction, nextData);
            }
        }
    } else {
        QString reason = root.value("reason").toString("Geçersiz kullanıcı adı / şifre veya oturum reddedildi.");
        emit loginFailed(reason);
        emit logMessage(QString("[CGI Giriş Başarısız] %1").arg(reason), true);
    }
}

void Ethernet::handleAlarmStatusResponse(const QJsonObject &root) {
    bool sig = false;
    bool vid = false;
    bool aud = false;
    QString chName;
    double pwr = 0.0;
    double cn = 0.0;
    double ber = 0.0;
    double ebn0 = 0.0;

    const QJsonArray allElements = extractElements(root);
    if (!allElements.isEmpty()) {
        for (const auto &val : allElements) {
            QJsonObject elem = val.toObject();
            QString domid = elem.value("domid").toString().trimmed().toLower();
            QString caption = elem.value("caption").toString().trimmed().toLower();
            QString strVal = elem.value("value").toString().trimmed();

            if (domid == "st_channel" || caption.contains("kanal") || caption.contains("channel")) {
                chName = strVal;
            } else if (domid == "st_signal" || caption == "sinyal" || caption == "signal") {
                sig = isDrxLockPositive(strVal);
            } else if (domid == "st_video" || caption.contains("video")) {
                vid = isDrxLockPositive(strVal);
            } else if (domid == "st_audio" || caption.contains("audio") || caption.contains("ses")) {
                aud = isDrxLockPositive(strVal);
            } else if (domid == "st_cn" || caption.contains("c/n") || caption.contains("cn")) {
                cn = parseDrxNumeric(strVal);
            } else if (domid == "st_power" || caption.contains("güç") || caption.contains("power")) {
                pwr = parseDrxNumeric(strVal);
            } else if (domid == "st_ber" || caption.contains("ber")) {
                ber = parseDrxNumeric(strVal);
            } else if (domid == "st_ebn0" || caption.contains("ebn0") || caption.contains("eb/n0")) {
                ebn0 = parseDrxNumeric(strVal);
            }
        }
    } else {
        // Backward compatibility for flat JSON fields
        if (root.contains("signalLock")) sig = root.value("signalLock").toBool(false);
        else if (root.contains("sLock")) sig = (root.value("sLock").toInt(0) == 1);

        if (root.contains("videoLock")) vid = root.value("videoLock").toBool(false);
        else if (root.contains("vLock")) vid = (root.value("vLock").toInt(0) == 1);

        if (root.contains("audioLock")) aud = root.value("audioLock").toBool(false);
        else if (root.contains("aLock")) aud = (root.value("aLock").toInt(0) == 1);

        chName = root.value("channelName").toString();
        pwr = root.value("powerDbm").toDouble(root.value("pow").toDouble(-99.0));
        cn = root.value("cnDb").toDouble(root.value("cn").toDouble(0.0));
        ber = root.value("ber").toDouble(1.0e-7);
        ebn0 = root.value("ebN0Db").toDouble(root.value("ebn0").toDouble(0.0));
    }

    emit statusReceived(sig, vid, aud, chName);
    emit statusReceivedFull(sig, vid, aud, chName);

    emit telemetryReceived(pwr, cn, ber, ebn0);
    emit telemetryReceivedFull(pwr, cn, ber, ebn0);
}

void Ethernet::handleDeviceInfoResponse(const QJsonObject &root) {
    QString sw, devId, mac, tuner;
    const QJsonArray allElements = extractElements(root);
    if (!allElements.isEmpty()) {
        for (const auto &val : allElements) {
            QJsonObject elem = val.toObject();
            QString domid = elem.value("domid").toString().toLower();
            QString cap = elem.value("caption").toString().toLower();
            QString v = elem.value("value").toString();

            if (domid == "di_sw" || cap.contains("sw") || cap.contains("yazılım") || cap.contains("versiyon")) {
                sw = v;
            } else if (domid == "di_devid" || cap.contains("cihaz id") || cap.contains("device id")) {
                devId = v;
            } else if (domid == "di_mac" || cap.contains("mac")) {
                mac = v;
            } else if (domid == "di_tuner" || cap.contains("tuner")) {
                tuner = v;
            }
        }
    } else {
        sw = root.value("swVersion").toString(root.value("sw").toString());
        devId = root.value("deviceId").toString(root.value("id").toString());
        mac = root.value("mac").toString();
        tuner = root.value("tunerModel").toString(root.value("tuner").toString());
    }

    emit deviceInfoReceived("sw_version", sw);
    emit deviceInfoReceived("device_id", devId);
    emit deviceInfoReceived("mac", mac);
    emit deviceInfoReceived("tuner_model", tuner);
    emit deviceInfoReceivedFull(sw, devId, mac, tuner);
}

void Ethernet::handleTuneResponse(const QJsonObject &root) {
    bool ok = root.value("status").toString().toLower() == "ok" || root.value("result").toBool(true);
    QString details = root.value("message").toString("Transponder ayarlandı");
    emit tuneCompleted(ok, details);
}

// =============================================================================
// 2. RS232 DRIVER (SERIAL PORT + INTERNAL PROTOCOL PARSER)
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
