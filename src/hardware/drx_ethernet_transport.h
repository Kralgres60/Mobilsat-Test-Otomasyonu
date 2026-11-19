/**
 * @file drx_ethernet_transport.h
 * @brief CAT6 Ethernet Web CGI (/cgi-bin/drx.cgi) and ICMP Ping driver inheriting DataTransfer.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_ETHERNET_TRANSPORT_H
#define DRX_ETHERNET_TRANSPORT_H

#include "drx_transport.h"
#include "drx_protocol_config.h"
#include <QUrl>
#include <QTimer>
#include <QVector>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

/**
 * @class Ethernet
 * @brief Full-featured driver using CAT6 Ethernet Web CGI (/cgi-bin/drx.cgi) and ICMP Ping.
 */
class Ethernet : public DataTransfer {
    Q_OBJECT
public:
    explicit Ethernet(QObject *parent = nullptr);
    explicit Ethernet(const QString &hostIp, int port = 80, QObject *parent = nullptr);
    ~Ethernet() override;

    QString transferType() const override { return "Ethernet"; }

    bool connectDevice() override;
    void disconnectDevice() override;
    bool isConnected() const override;
    bool isDeviceReady() const override;

    void sendData(const QByteArray &data) override;
    void sendTune(int freqMhz, int symbolRate, const QString &polarity) override;
    void requestStatus() override;
    void requestDeviceInfo() override;
    void sendRaw(const QString &command) override;

    void setHost(const QString &hostIp, int port = 80);
    QString host() const { return m_hostIp; }
    int port() const { return m_port; }

    void setCredentials(const QString &username, const QString &password);
    QString username() const { return m_username; }
    QString password() const { return m_password; }

    bool isReachable() const { return m_isReachable; }
    int lastPingLatencyMs() const { return m_lastLatencyMs; }
    bool isLoggedIn() const { return m_sessionKey > 0; }
    int sessionKey() const { return m_sessionKey; }

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    int timeoutMs() const { return m_timeoutMs; }
    void resetSession();

    /** @brief Get the last parsed device clock timestamp. @return Device QDateTime. */
    QDateTime lastDeviceTime() const { return m_lastDeviceTime; }

    /** @brief Get the host arrival timestamp when telemetry was parsed. @return Host QDateTime. */
    QDateTime lastHostTime() const { return m_lastHostTime; }

    /** @brief Get the raw unparsed device timestamp string from DRX alarmstatus JSON. @return Raw time string. */
    QString lastDeviceTimeString() const { return m_lastDeviceTimeStr; }

    /** @brief Returns collected time pairs history. */
    const QVector<TimestampPair>& timestampHistory() const { return m_timestampHistory; }

    /** @brief Clears collected timestamp pairs. */
    void clearTimestampHistory() { m_timestampHistory.clear(); }

    /** @brief Get the latency/clock difference in milliseconds (Host - Device). @return Delta time in ms. */
    qint64 lastDeltaTimeMs() const { return m_lastDeltaTimeMs; }

    /** @brief Get the latency/clock difference in seconds (Host - Device). @return Delta time in seconds. */
    double deltaTimeSeconds() const { return m_lastDeltaTimeMs / 1000.0; }

public slots:
    void pingHost(int timeoutMs = 1000);
    void testConnectionAndSync();
    void login();
    void sendRawQuery(const QString &queryString) { sendRaw(queryString); }

signals:
    void pingFinished(bool reachable, int latencyMs, const QString &hostIp);
    void loginSucceeded(int sessionKey);
    void loginFailed(const QString &reason);
    void tuneCompleted(bool isOk, const QString &details);
    void rawResponseReceived(const QString &actionName, const QByteArray &rawJson);

    // Extended signals for DrxWebClient compatibility
    void statusReceivedFull(bool signalLocked, bool videoProducing, bool audioProducing, const QString &channelName);
    void telemetryReceivedFull(double powerDbm, double cnDb, double ber, double ebN0Db);
    void deviceInfoReceivedFull(const QString &swVersion, const QString &deviceId, const QString &mac, const QString &tunerModel);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QUrl cgiUrl() const;
    void sendCgiRequest(const QString &actionType, const QByteArray &postData);
    void parseCgiResponse(const QString &actionType, const QByteArray &payload);

    void handleLoginResponse(const QJsonObject &root);
    void handleAlarmStatusResponse(const QJsonObject &root);
    void handleDeviceInfoResponse(const QJsonObject &root);
    void handleTuneResponse(const QJsonObject &root);

    QNetworkAccessManager m_netManager;
    QString m_hostIp = "192.168.0.167";
    int m_port = 80;
    QString m_username = "admin";
    QString m_password = "1234";
    int m_sessionKey = 0;
    int m_timeoutMs = 4000;

    bool m_isReachable = false;
    int m_lastLatencyMs = -1;
    bool m_pingInProgress = false;
    bool m_syncAfterPing = false;

    QString m_pendingActionAfterLogin;
    QByteArray m_pendingDataAfterLogin;

    QDateTime m_lastDeviceTime;
    QDateTime m_lastHostTime;
    QString m_lastDeviceTimeStr;
    qint64 m_lastDeltaTimeMs = 0;
    QVector<TimestampPair> m_timestampHistory;
};

using EthernetTransfer = Ethernet;
using DrxWebClient = Ethernet;

#endif // DRX_ETHERNET_TRANSPORT_H
