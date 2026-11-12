/**
 * @file drx_sim_server.h
 * @brief TCP/HTTP-CGI virtual NETA DRX9000 receiver server serving live telemetry samples across LAN.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_SIM_SERVER_H
#define DRX_SIM_SERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QJsonObject>
#include "sim_curve_model.h"

/**
 * @class DrxSimServer
 * @brief Listens on a configurable TCP port (supporting LAN 0.0.0.0 binding) and serves
 *        NETA DRX9000 HTTP/CGI responses (`login`, `deviceInfo`, `freq`, `alarmStatus`)
 *        driven directly by the interactive graph's live sample point.
 */
class DrxSimServer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs a virtual DRX9000 TCP server instance.
     * @param deviceId Simulated hardware device identifier reported in `deviceInfo`.
     * @param macAddr Simulated MAC address reported in `deviceInfo`.
     * @param parent Optional parent QObject.
     */
    explicit DrxSimServer(const QString &deviceId = "DRX-SIM-01",
                          const QString &macAddr = "00:1A:2B:90:00:01",
                          QObject *parent = nullptr);
    ~DrxSimServer() override;

    /**
     * @brief Starts listening on the given bind IP (`0.0.0.0` for all LAN interfaces) and TCP port.
     * @param bindIp Interface IPv4 address (`0.0.0.0` listens on all adapters).
     * @param port TCP port number (e.g., 80 or 8080).
     * @return True if TCP listen succeeded.
     */
    bool start(const QString &bindIp, quint16 port);

    /**
     * @brief Stops the TCP server and closes all open client sockets.
     */
    void stop();

    /**
     * @brief Returns whether the server is actively listening on TCP.
     * @return True if listening.
     */
    bool isListening() const { return m_server.isListening(); }

    /**
     * @brief Returns the configured bind IP string.
     */
    QString bindIp() const { return m_bindIp; }

    /**
     * @brief Returns the bound TCP port.
     */
    quint16 port() const { return m_port; }

    /**
     * @brief Returns the last listening error string if `start()` failed.
     */
    QString lastError() const { return m_server.errorString(); }

    /**
     * @brief Updates the active telemetry point served to subsequent `alarmStatus` requests.
     * @param sample Current curve sample (C/N, Power, locks, BER).
     */
    void setLiveSample(const SimTelemetryPoint &sample);

    /**
     * @brief Enables or disables ±0.15 dB Gaussian-like micro-jitter on locked readings.
     * @param enabled True to apply realistic micro-jitter.
     */
    void setNoiseEnabled(bool enabled) { m_noiseEnabled = enabled; }

    /**
     * @brief Forces the next authenticated CGI request to receive `access=denied`
     *        to verify automatic re-login recovery in the main test client.
     */
    void expireSessionKey();

    /**
     * @brief Resets total handled request counter to zero.
     */
    void resetRequestCount() { m_requestCount = 0; }

signals:
    /**
     * @brief Emitted whenever a TCP client request is parsed and answered.
     * @param totalCount Cumulative handled requests.
     * @param peerIp Remote client IPv4 address.
     * @param actionTag CGI action identifier (`LOGIN`, `TUNE`, `STATUS`, `GET_STATUS`, etc.).
     */
    void requestHandled(quint64 totalCount, const QString &peerIp, const QString &actionTag);

    /**
     * @brief Emitted when a remote client sends a `ResponseTo=freq` tune command.
     * @param freqMhz Tuned frequency in MHz.
     * @param symbolRate Tuned symbol rate in kS/s.
     * @param pol Polarization (`H` or `V`).
     */
    void tuned(int freqMhz, int symbolRate, const QString &pol);

    /**
     * @brief Emitted for human-readable TCP activity logging.
     * @param message Log entry text.
     */
    void logMessage(const QString &message);

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    void handleHttpRequest(QTcpSocket *socket, const QByteArray &rawRequest);
    QJsonObject buildAlarmStatusJson() const;
    QByteArray buildJsonResponse(const QJsonObject &obj, int httpCode = 200, const QString &statusText = "OK") const;
    QString resolveChannelName(int freqMhz, const QString &pol) const;
    static QString cleanPeerAddress(const QTcpSocket *socket);

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;

    QString m_deviceId;
    QString m_macAddr;
    QString m_bindIp = "0.0.0.0";
    quint16 m_port   = 80;

    SimTelemetryPoint m_liveSample;
    bool    m_noiseEnabled      = false;
    bool    m_forceAccessDenied = false;
    int     m_activeSessionKey  = 482910;

    int     m_currentFreqMhz    = 11977;
    int     m_currentSymbolRate = 27500;
    QString m_currentPol        = "H";
    bool    m_isTuning          = false;
    quint64 m_requestCount      = 0;
};

#endif // DRX_SIM_SERVER_H
