/**
 * @file drx_web_client.cpp
 * @brief Implementation of the NETA DRX9000 Ethernet Ping + Web CGI HTTP client.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "drx_web_client.h"
#include <QDebug>
#include <QJsonValue>
#include <QRegularExpression>
#include <QProcess>
#include <QPointer>

// --- Helper Numeric and Lock Parsing Utilities ---

/**
 * @brief Parses numeric values from DRX telemetry strings, handling spaced signs, scientific notation, and units.
 * @param str Input string (e.g. "- 39 dBm", "14.6 dB", "1.0E-7")
 * @return Extracted floating-point value
 */
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

/**
 * @brief Evaluates whether a DRX status string signifies a positive locked state.
 * @param str Status token string (e.g. "tick", "cross", "1", "true")
 * @return true if locked; false if unlocked
 */
static bool isDrxLockPositive(QString str) {
    str = str.trimmed().toLower();
    if (str.isEmpty() || str == "cross" || str == "?" || str == "0" || str == "false" || str == "no" || str == "unlocked") {
        return false;
    }
    return (str == "tick" || str.contains("tick") || str == "ok" || str == "true" || str == "1" || str.contains("lock") || str == "yes");
}

DrxWebClient::DrxWebClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_netManager, &QNetworkAccessManager::finished,
            this, &DrxWebClient::onReplyFinished);
}

DrxWebClient::~DrxWebClient()
{
}

void DrxWebClient::setHost(const QString &hostIp, int port)
{
    QString cleanHost = hostIp.trimmed();
    if (cleanHost.startsWith("http://", Qt::CaseInsensitive)) {
        cleanHost = cleanHost.mid(7);
    } else if (cleanHost.startsWith("https://", Qt::CaseInsensitive)) {
        cleanHost = cleanHost.mid(8);
    }

    QString newIp = cleanHost;
    int newPort = port;
    if (cleanHost.contains(':')) {
        QStringList parts = cleanHost.split(':');
        newIp = parts[0];
        newPort = parts[1].toInt();
    }

    if (newIp != m_hostIp || newPort != m_port) {
        m_hostIp = newIp;
        m_port = newPort;
        m_isReachable = false;
        m_lastLatencyMs = -1;
        resetSession();
    }
}

void DrxWebClient::setCredentials(const QString &username, const QString &password)
{
    m_username = username;
    m_password = password;
}

void DrxWebClient::resetSession()
{
    m_sessionKey = 0;
    m_pendingActionAfterLogin.clear();
    m_pendingDataAfterLogin.clear();
}

void DrxWebClient::pingHost(int timeoutMs)
{
    if (m_hostIp.trimmed().isEmpty()) {
        m_isReachable = false;
        m_lastLatencyMs = -1;
        emit pingFinished(false, -1, m_hostIp);
        return;
    }
    if (m_pingInProgress) {
        return;
    }

    m_pingInProgress = true;
    QString targetIp = m_hostIp;
    QProcess *pingProc = new QProcess(this);
    QPointer<QProcess> safeProc(pingProc);

    connect(pingProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, pingProc, targetIp](int exitCode, QProcess::ExitStatus) {
        m_pingInProgress = false;
        QString output = QString::fromLocal8Bit(pingProc->readAllStandardOutput());
        pingProc->deleteLater();

        // Windows ping.exe can return 0 on "Destination host unreachable"; verify genuine ICMP TTL= reply.
        bool hasTtl = output.contains("TTL=", Qt::CaseInsensitive);
        bool reachable = (exitCode == 0) && (hasTtl || output.contains("ttl=", Qt::CaseInsensitive));

        int latencyMs = -1;
        if (reachable) {
            static QRegularExpression rttRegex(R"((?:time|s[üu]re)\s*([<=])\s*([0-9]+)\s*ms)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = rttRegex.match(output);
            if (match.hasMatch()) {
                latencyMs = qMax(1, match.captured(2).toInt());
            } else {
                latencyMs = 1;
            }
        }

        bool wasReachable = m_isReachable;
        m_isReachable = reachable;
        m_lastLatencyMs = latencyMs;

        emit pingFinished(reachable, latencyMs, targetIp);

        if (!reachable) {
            if (wasReachable || isLoggedIn()) {
                emit logMessage(QString("[ETH PING] %1 yanıt vermiyor; oturum sıfırlandı.").arg(targetIp), true);
            }
            resetSession();
            m_syncAfterPing = false;
        } else {
            bool justCameOnline = !wasReachable;
            if (m_syncAfterPing || justCameOnline || !isLoggedIn()) {
                m_syncAfterPing = false;
                if (!isLoggedIn()) {
                    emit logMessage(QString("[ETH PING] %1 çevrimiçi (%2 ms). Web CGI oturumu açılıyor...")
                                        .arg(targetIp).arg(latencyMs), false);
                    m_pendingActionAfterLogin = "SYNC_CHAIN";
                    login();
                } else {
                    requestDeviceInfo();
                    requestStatus();
                }
            }
        }
    });

    connect(pingProc, &QProcess::errorOccurred, this, [this, pingProc, targetIp](QProcess::ProcessError) {
        m_pingInProgress = false;
        m_isReachable = false;
        m_lastLatencyMs = -1;
        m_syncAfterPing = false;
        resetSession();
        pingProc->deleteLater();
        emit pingFinished(false, -1, targetIp);
    });

#ifdef Q_OS_WIN
    pingProc->start("ping", QStringList() << "-n" << "1" << "-w" << QString::number(timeoutMs) << targetIp);
#else
    pingProc->start("ping", QStringList() << "-c" << "1" << "-W" << QString::number(qMax(1, timeoutMs / 1000)) << targetIp);
#endif

    QTimer::singleShot(timeoutMs + 1500, this, [safeProc]() {
        if (safeProc && safeProc->state() != QProcess::NotRunning) {
            safeProc->kill();
        }
    });
}

void DrxWebClient::testConnectionAndSync()
{
    emit logMessage(QString("[ETH SYNC] %1 için ping testi ve veri yenileme başlatıldı...").arg(m_hostIp), false);
    m_syncAfterPing = true;
    pingHost(1000);
}

QUrl DrxWebClient::cgiUrl() const
{
    QUrl url;
    url.setScheme("http");
    url.setHost(m_hostIp);
    url.setPort(m_port);
    url.setPath("/cgi-bin/drx.cgi");
    return url;
}

void DrxWebClient::login()
{
    // CGI Login Payload Format:
    // ResponseTo=login\nsessionKey=\nlg_username=admin\nlg_password=1234\n
    QByteArray postData;
    postData.append("ResponseTo=login\n");
    postData.append("sessionKey=\n");
    postData.append("lg_username=" + m_username.toUtf8() + "\n");
    postData.append("lg_password=" + m_password.toUtf8() + "\n");

    emit logMessage(QString("[DRX CGI] Logging in: %1@%2:%3").arg(m_username, m_hostIp).arg(m_port), false);
    sendCgiRequest("LOGIN", postData);
}

void DrxWebClient::requestStatus()
{
    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "STATUS";
        login();
        return;
    }

    QByteArray postData;
    postData.append("query=alarmStatus\n");
    postData.append(QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());

    sendCgiRequest("STATUS", postData);
}

void DrxWebClient::requestDeviceInfo()
{
    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "DEVICE_INFO";
        login();
        return;
    }

    QByteArray postData;
    postData.append("query=deviceInfo\n");
    postData.append(QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());

    sendCgiRequest("DEVICE_INFO", postData);
}

void DrxWebClient::sendTune(int freqMHz, int symbolRate, const QString &polarity)
{
    int freqKhz = (freqMHz < 50000) ? (freqMHz * 1000) : freqMHz;
    int srBdps = (symbolRate < 100000) ? (symbolRate * 1000) : symbolRate;
    int polEnum = (polarity.trimmed().toUpper().startsWith('H')) ? 1 : 0;

    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "TUNE";
        QByteArray postData;
        postData.append("ResponseTo=freq\n");
        postData.append("sessionKey=\n");
        postData.append(QString("fr_frequency=%1\n").arg(freqKhz).toUtf8());
        postData.append(QString("fr_srate=%1\n").arg(srBdps).toUtf8());
        postData.append(QString("fr_pol=%1\n").arg(polEnum).toUtf8());
        m_pendingDataAfterLogin = postData;
        login();
        return;
    }

    QByteArray postData;
    postData.append("ResponseTo=freq\n");
    postData.append(QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());
    postData.append(QString("fr_frequency=%1\n").arg(freqKhz).toUtf8());
    postData.append(QString("fr_srate=%1\n").arg(srBdps).toUtf8());
    postData.append(QString("fr_pol=%1\n").arg(polEnum).toUtf8());

    emit logMessage(QString("[DRX CGI] Tuning transponder: %1 MHz, %2 kS/s, Pol: %3")
                        .arg(freqMHz).arg(symbolRate).arg(polarity), false);

    sendCgiRequest("TUNE", postData);
}

void DrxWebClient::sendRawQuery(const QString &queryString)
{
    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "RAW_" + queryString;
        login();
        return;
    }

    QByteArray postData;
    postData.append(QString("query=%1\n").arg(queryString).toUtf8());
    postData.append(QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());

    sendCgiRequest("RAW_" + queryString, postData);
}

void DrxWebClient::sendCgiRequest(const QString &actionType, const QByteArray &postData)
{
    QNetworkRequest request(cgiUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setHeader(QNetworkRequest::UserAgentHeader, "NETA-DRX9000-Automator");
    request.setAttribute(QNetworkRequest::Attribute(QNetworkRequest::User + 1), actionType);

    QNetworkReply *reply = m_netManager.post(request, postData);

    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply, this, actionType]() {
        if (reply->isRunning()) {
            reply->abort();
            emit logMessage(QString("[DRX CGI TIMEOUT] %1 request timed out.").arg(actionType), true);
        }
    });
    timer->start(m_timeoutMs);
}

void DrxWebClient::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    QString actionType = reply->request().attribute(QNetworkRequest::Attribute(QNetworkRequest::User + 1)).toString();

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit logMessage(QString("[DRX CGI ERROR] %1: %2").arg(actionType, reply->errorString()), true);
            if (actionType == "LOGIN") {
                emit loginFailed(reply->errorString());
            } else if (reply->error() == QNetworkReply::ConnectionRefusedError ||
                       reply->error() == QNetworkReply::HostNotFoundError ||
                       reply->error() == QNetworkReply::RemoteHostClosedError) {
                resetSession();
            }
        }
        return;
    }

    QByteArray payload = reply->readAll();
    emit rawResponseReceived(actionType, payload);

    parseCgiResponse(actionType, payload);
}

void DrxWebClient::parseCgiResponse(const QString &actionType, const QByteArray &payload)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit logMessage(QString("[DRX CGI] Invalid JSON response payload (%1)").arg(actionType), true);
        return;
    }

    QJsonObject root = doc.object();

    // Check for authorization rejection (access: denied)
    if (root.value("access").toString() == "denied") {
        emit logMessage("[DRX CGI] Session key expired or access denied! Re-authenticating...", true);
        m_sessionKey = 0;
        m_pendingActionAfterLogin = actionType;
        login();
        return;
    }

    if (actionType == "LOGIN") {
        handleLoginResponse(root);
    } else if (actionType == "STATUS") {
        handleAlarmStatusResponse(root);
    } else if (actionType == "DEVICE_INFO") {
        handleDeviceInfoResponse(root);
    } else if (actionType == "TUNE") {
        handleTuneResponse(root);
    }
}

void DrxWebClient::handleLoginResponse(const QJsonObject &root)
{
    if (root.contains("sessionkey")) {
        m_sessionKey = root.value("sessionkey").toInt();
        emit logMessage(QString("[DRX CGI] Login successful! SessionKey: %1").arg(m_sessionKey), false);
        emit loginSucceeded(m_sessionKey);

        // Dispatch queued requests awaiting authentication
        if (!m_pendingActionAfterLogin.isEmpty()) {
            QString action = m_pendingActionAfterLogin;
            m_pendingActionAfterLogin.clear();

            if (action == "SYNC_CHAIN") {
                requestDeviceInfo();
                requestStatus();
            } else if (action == "STATUS") {
                requestStatus();
            } else if (action == "DEVICE_INFO") {
                requestDeviceInfo();
            } else if (action == "TUNE" && !m_pendingDataAfterLogin.isEmpty()) {
                QByteArray updatedData = m_pendingDataAfterLogin;
                updatedData.replace("sessionKey=\n", QString("sessionKey=%1\n").arg(m_sessionKey).toUtf8());
                m_pendingDataAfterLogin.clear();
                sendCgiRequest("TUNE", updatedData);
            }
        }
    } else {
        QString reason = "Authentication failed (Invalid username or password).";
        emit logMessage("[DRX CGI] " + reason, true);
        emit loginFailed(reason);
    }
}

/**
 * @brief Extracts and flattens element objects from nested DRX CGI JSON sections.
 * @param root Parsed CGI JSON object
 * @return Combined JSON array of field elements
 */
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

void DrxWebClient::handleAlarmStatusResponse(const QJsonObject &root)
{
    QString channelName;
    QString deviceTime;
    bool sigLock = false;
    bool vidLock = false;
    bool audLock = false;
    double cn = 0.0;
    double pwr = 0.0;
    double ber = 0.0;
    double ebn0 = 0.0;

    const QJsonArray allElements = extractElements(root);

    for (const auto &val : allElements) {
        QJsonObject elem = val.toObject();
        QString domid = elem.value("domid").toString().trimmed().toLower();
        QString caption = elem.value("caption").toString().trimmed().toLower();
        QString strVal = elem.value("value").toString().trimmed();

        if (domid == "st_current_time" || caption.contains("saat") || caption.contains("time")) {
            deviceTime = strVal;
        }
        else if (domid == "st_channel" || caption.contains("kanal") || caption.contains("channel")) {
            channelName = strVal;
        }
        else if (domid == "st_signal" || caption == "sinyal" || caption == "signal") {
            sigLock = isDrxLockPositive(strVal);
        }
        else if (domid == "st_video" || caption.contains("video")) {
            vidLock = isDrxLockPositive(strVal);
        }
        else if (domid == "st_audio" || caption.contains("audio") || caption.contains("ses")) {
            audLock = isDrxLockPositive(strVal);
        }
        else if (domid == "st_cn" || caption.contains("c/n") || caption.contains("cn")) {
            cn = parseDrxNumeric(strVal);
        }
        else if (domid == "st_power" || caption.contains("güç") || caption.contains("gucu") || caption.contains("power")) {
            pwr = parseDrxNumeric(strVal);
        }
        else if (domid == "st_ber" || caption.contains("ber")) {
            ber = parseDrxNumeric(strVal);
        }
        else if (domid == "st_ebn0" || caption.contains("ebn0") || caption.contains("eb/n0")) {
            ebn0 = parseDrxNumeric(strVal);
        }
    }

    emit statusReceived(sigLock, vidLock, audLock, channelName);
    emit telemetryReceived(pwr, cn, ber, ebn0);

    emit logMessage(QString("[DRX TELEMETRY] Time: %1 | Ch: '%2' | Sig: %3 | Vid: %4 | C/N: %5 dB | Pow: %6 dBm")
                        .arg(deviceTime.isEmpty() ? "-" : deviceTime)
                        .arg(channelName)
                        .arg(sigLock ? "LOCKED" : "UNLOCKED")
                        .arg(vidLock ? "VAR" : "YOK")
                        .arg(cn, 0, 'f', 2)
                        .arg(pwr, 0, 'f', 2), false);
}

void DrxWebClient::handleDeviceInfoResponse(const QJsonObject &root)
{
    const QJsonArray allElements = extractElements(root);

    QString swVer, devId, mac, tunerModel;

    for (const auto &val : allElements) {
        QJsonObject elem = val.toObject();
        QString cap = elem.value("caption").toString().toLower();
        QString domid = elem.value("domid").toString().toLower();
        QString v = elem.value("value").toString();

        if (cap.contains("sw") || cap.contains("yazılım") || cap.contains("versiyon") || cap.contains("version") || domid.contains("sw")) {
            if (swVer.isEmpty()) swVer = v;
        }
        else if (cap.contains("cihaz id") || cap.contains("device id") || domid.contains("devid")) {
            devId = v;
        }
        else if (cap.contains("mac") || domid.contains("mac")) {
            mac = v;
        }
        else if (cap.contains("tuner") || domid.contains("tuner")) {
            tunerModel = v;
        }
    }

    emit deviceInfoReceived(swVer, devId, mac, tunerModel);
    emit logMessage(QString("[DRX DEVICE INFO] SW: %1 | ID: %2 | MAC: %3 | Tuner: %4")
                        .arg(swVer, devId, mac, tunerModel), false);
}

void DrxWebClient::handleTuneResponse(const QJsonObject &root)
{
    Q_UNUSED(root);
    emit logMessage("[DRX CGI] Transponder frequency configured successfully.", false);
    emit tuneCompleted(true, "Transponder tuned.");
}
