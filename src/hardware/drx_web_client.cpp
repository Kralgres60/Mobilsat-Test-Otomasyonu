/**
 * @file drx_web_client.cpp
 * @brief Implementation of the NETA DRX9000 Web CGI HTTP client.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "drx_web_client.h"
#include <QDebug>
#include <QJsonValue>
#include <QRegularExpression>

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
    // Remove space between sign and digits (e.g., "- 39 dBm" -> "-39 dBm")
    static QRegularExpression signSpaceRegex(R"(([-+])\s+)");
    str.replace(signSpaceRegex, R"(\1)");
    // Normalize localized comma decimals to period
    str.replace(',', '.');
    // Extract signed floating point number including scientific exponent (-45.2, 14.5 dB, 1.0E-7)
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

    if (cleanHost.contains(':')) {
        QStringList parts = cleanHost.split(':');
        m_hostIp = parts[0];
        m_port = parts[1].toInt();
    } else {
        m_hostIp = cleanHost;
        m_port = port;
    }
}

void DrxWebClient::setCredentials(const QString &username, const QString &password)
{
    m_username = username;
    m_password = password;
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

    // CGI Status Format:
    // query=alarmStatus\nsessionKey=XXXX\n
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
    // DRX expects frequency in kHz and symbol rate in Bdps
    // Example: 12053 MHz -> 12053000 kHz, 27500 kSym/s -> 27500000 Bdps
    // Polarization: 0 = Vertical (V), 1 = Horizontal (H)
    int freqKhz = (freqMHz < 50000) ? (freqMHz * 1000) : freqMHz;
    int srBdps = (symbolRate < 100000) ? (symbolRate * 1000) : symbolRate;
    int polEnum = (polarity.trimmed().toUpper().startsWith('H')) ? 1 : 0;

    if (m_sessionKey <= 0) {
        m_pendingActionAfterLogin = "TUNE";
        // Cache parameters to dispatch immediately upon login
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

        // Dispatch any queued requests awaiting authentication
        if (!m_pendingActionAfterLogin.isEmpty()) {
            QString action = m_pendingActionAfterLogin;
            m_pendingActionAfterLogin.clear();

            if (action == "STATUS") {
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

    // Aggregate elements across all sections and root level
    QJsonArray allElements;
    if (root.contains("sections")) {
        QJsonArray sections = root.value("sections").toArray();
        for (const auto &secVal : sections) {
            QJsonObject secObj = secVal.toObject();
            if (secObj.contains("elements")) {
                QJsonArray subElems = secObj.value("elements").toArray();
                for (const auto &e : subElems) {
                    allElements.append(e);
                }
            }
        }
    }
    if (root.contains("elements")) {
        QJsonArray rootElems = root.value("elements").toArray();
        for (const auto &e : rootElems) {
            allElements.append(e);
        }
    }

    emit logMessage(QString("[DRX PARSE] Found %1 elements in alarmStatus payload.").arg(allElements.size()), false);

    for (const auto &val : allElements) {
        QJsonObject elem = val.toObject();
        QString domid = elem.value("domid").toString().trimmed().toLower();
        QString caption = elem.value("caption").toString().trimmed().toLower();
        QString strVal = elem.value("value").toString().trimmed();

        emit logMessage(QString("  -> [DOM: %1 | Caption: %2] Raw: '%3'").arg(domid, caption, strVal), false);

        // Timestamp / Clock
        if (domid == "st_current_time" || caption.contains("saat") || caption.contains("time")) {
            deviceTime = strVal;
        }
        // Channel Name
        else if (domid == "st_channel" || caption.contains("kanal") || caption.contains("channel")) {
            channelName = strVal;
        }
        // Signal / Carrier Lock
        else if (domid == "st_signal" || caption == "sinyal" || caption == "signal") {
            sigLock = isDrxLockPositive(strVal);
        }
        // Video Lock
        else if (domid == "st_video" || caption.contains("video")) {
            vidLock = isDrxLockPositive(strVal);
        }
        // Audio Lock
        else if (domid == "st_audio" || caption.contains("audio") || caption.contains("ses")) {
            audLock = isDrxLockPositive(strVal);
        }
        // C/N
        else if (domid == "st_cn" || caption.contains("c/n") || caption.contains("cn")) {
            cn = parseDrxNumeric(strVal);
        }
        // RF Power
        else if (domid == "st_power" || caption.contains("güç") || caption.contains("gucu") || caption.contains("power")) {
            pwr = parseDrxNumeric(strVal);
        }
        // Bit Error Rate (BER)
        else if (domid == "st_ber" || caption.contains("ber")) {
            ber = parseDrxNumeric(strVal);
        }
        // Eb/N0
        else if (domid == "st_ebn0" || caption.contains("ebn0") || caption.contains("eb/n0")) {
            ebn0 = parseDrxNumeric(strVal);
        }
    }

    emit statusReceived(sigLock, vidLock, audLock, channelName);
    emit telemetryReceived(pwr, cn, ber, ebn0);

    emit logMessage(QString("[DRX TELEMETRY RESULT] Time: %1 | Channel: '%2' | Signal: %3 | Video: %4 | Audio: %5 | C/N: %6 dB | Power: %7 dBm | EbN0: %8 dB | BER: %9")
                        .arg(deviceTime.isEmpty() ? "-" : deviceTime)
                        .arg(channelName)
                        .arg(sigLock ? "LOCKED (OK)" : "UNLOCKED (FAIL)")
                        .arg(vidLock ? "PRESENT" : "NONE")
                        .arg(audLock ? "PRESENT" : "NONE")
                        .arg(cn, 0, 'f', 2)
                        .arg(pwr, 0, 'f', 2)
                        .arg(ebn0, 0, 'f', 2)
                        .arg(QString::number(ber, 'e', 2)), false);
}

void DrxWebClient::handleDeviceInfoResponse(const QJsonObject &root)
{
    QJsonArray allElements;
    if (root.contains("sections")) {
        QJsonArray sections = root.value("sections").toArray();
        for (const auto &secVal : sections) {
            QJsonObject secObj = secVal.toObject();
            if (secObj.contains("elements")) {
                QJsonArray subElems = secObj.value("elements").toArray();
                for (const auto &e : subElems) {
                    allElements.append(e);
                }
            }
        }
    }
    if (root.contains("elements")) {
        QJsonArray rootElems = root.value("elements").toArray();
        for (const auto &e : rootElems) {
            allElements.append(e);
        }
    }

    QString swVer, devId, mac, tunerModel;

    for (const auto &val : allElements) {
        QJsonObject elem = val.toObject();
        QString cap = elem.value("caption").toString().toLower();
        QString v = elem.value("value").toString();

        if (cap.contains("sw") || cap.contains("yazılım") || cap.contains("versiyon") || cap.contains("version")) swVer = v;
        else if (cap.contains("cihaz id") || cap.contains("device id")) devId = v;
        else if (cap.contains("mac")) mac = v;
        else if (cap.contains("tuner")) tunerModel = v;
    }

    emit deviceInfoReceived(swVer, devId, mac, tunerModel);
    emit logMessage(QString("[DRX DEVICE INFO] SW: %1 | Model: %2 | MAC: %3 | Tuner: %4")
                        .arg(swVer, devId, mac, tunerModel), false);
}

void DrxWebClient::handleTuneResponse(const QJsonObject &root)
{
    Q_UNUSED(root);
    emit logMessage("[DRX CGI] Transponder frequency configured successfully.", false);
    emit tuneCompleted(true, "Transponder tuned.");
}
