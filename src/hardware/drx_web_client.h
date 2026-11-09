/**
 * @file drx_web_client.h
 * @brief NETA DRX9000 embedded Web CGI HTTP client for session management, RF tuning, and telemetry parsing.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_WEB_CLIENT_H
#define DRX_WEB_CLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

/**
 * @class DrxWebClient
 * @brief High-performance asynchronous HTTP client communicating with the DRX9000 embedded Web CGI
 *        endpoint (/cgi-bin/drx.cgi) using dynamic sessionKey tokens and multi-section JSON parsing.
 */
class DrxWebClient : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs DRX Web CGI client.
     * @param parent Parent QObject
     */
    explicit DrxWebClient(QObject *parent = nullptr);

    /** @brief Destroys client and cleans up network connections. */
    ~DrxWebClient() override;

    /** @brief Configures target receiver IP and port. */
    void setHost(const QString &hostIp, int port = 80);
    QString host() const { return m_hostIp; }
    int port() const { return m_port; }

    /** @brief Sets login authentication credentials. */
    void setCredentials(const QString &username, const QString &password);
    QString username() const { return m_username; }
    QString password() const { return m_password; }

    /** @brief Checks if a valid session key has been acquired. */
    bool isLoggedIn() const { return m_sessionKey > 0; }
    int sessionKey() const { return m_sessionKey; }

    /** @brief Configures HTTP request timeout in milliseconds. */
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    int timeoutMs() const { return m_timeoutMs; }

public slots:
    /** @brief Performs CGI authentication and acquires dynamic sessionKey (ResponseTo=login). */
    void login();

    /** @brief Interrogates live RF status and telemetry table (query=alarmStatus). */
    void requestStatus();

    /** @brief Interrogates receiver hardware/firmware identification (query=deviceInfo). */
    void requestDeviceInfo();

    /**
     * @brief Tunes receiver to a specific satellite transponder.
     * @param freqMHz Downlink frequency in MHz
     * @param symbolRate Symbol rate in kSym/s
     * @param polarity "V" (Vertical) or "H" (Horizontal)
     */
    void sendTune(int freqMHz, int symbolRate, const QString &polarity);

    /** @brief Sends arbitrary raw CGI query command to receiver (e.g. channeldetails, frequency). */
    void sendRawQuery(const QString &queryString);

signals:
    /** @brief Emitted upon successful CGI authentication with active session key. */
    void loginSucceeded(int sessionKey);

    /** @brief Emitted when authentication fails. */
    void loginFailed(const QString &reason);

    /** @brief Emitted when carrier, video, or audio lock status updates. */
    void statusReceived(bool signalLocked, bool videoProducing, bool audioProducing, const QString &channelName);

    /** @brief Emitted when numeric RF telemetry parameters update. */
    void telemetryReceived(double powerDbm, double cnDb, double ber, double ebN0Db);

    /** @brief Emitted when device identification metadata is received. */
    void deviceInfoReceived(const QString &swVersion, const QString &deviceId, const QString &mac, const QString &tunerModel);

    /** @brief Emitted when transponder tuning request completes. */
    void tuneCompleted(bool isOk, const QString &details);

    /** @brief Emitted with pretty-printed raw payload for developer debugging. */
    void rawResponseReceived(const QString &actionName, const QByteArray &rawJson);

    /** @brief Informational or error logging message. */
    void logMessage(const QString &msg, bool isError);

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

    QString m_pendingActionAfterLogin;
    QByteArray m_pendingDataAfterLogin;
};

#endif // DRX_WEB_CLIENT_H
