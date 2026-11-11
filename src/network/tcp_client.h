/**
 * @file tcp_client.h
 * @brief TCP Client communicating with Central Host for mutual exclusion and reference telemetry (Architecture 1).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>
#include "core/data_models.h"

/**
 * @class TcpClient
 * @brief Operates on each factory test station, connecting to the Host TCP server (:9100),
 *        maintaining keep-alive heartbeats, requesting reference locks, and receiving reference RF telemetry.
 */
class TcpClient : public QObject {
    Q_OBJECT
public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient() override;

    /** @brief Connects to Host TCP command server. */
    void connectToHost(const QString &hostIp, quint16 port = 9100);

    /** @brief Disconnects from host. */
    void disconnectFromHost();

    /** @brief Checks connection state. */
    bool isConnected() const;

    /** @brief Registers station ID and name upon connecting. */
    void registerStation(int stationId, const QString &stationName);

    /** @brief Dispatches periodic heartbeat telemetry. */
    void sendHeartbeat(const QString &state, int currentChannel = 0, const QString &antennaSn = QString());

    /** @brief Requests mutual exclusion lock for Reference DRX9000. */
    void requestLock(int timeoutSec = 90);

    /** @brief Releases held reference lock. */
    void releaseLock();

    /** @brief Requests Host to tune the Reference DRX to target channel. */
    void requestRefTune(int channelNo, int freqMHz, int symbolRate, const QString &polarity);

    /** @brief Returns the active reference comparison mode dictated by the Host. */
    ReferenceMode referenceMode() const { return m_referenceMode; }

signals:
    void connected();
    void disconnected();
    void registered();
    void lockGranted(int timeoutSec);
    void lockWaiting(int queuePosition, int activeStationId);
    void lockExpired(const QString &reason);
    void refDataReceived(double refCn, double refPow, bool carrierLocked, bool videoLocked);
    /** @brief Emitted when Host updates or sends the active reference mode (Live vs Golden Sample). */
    void referenceModeChanged(ReferenceMode mode);
    void logMessage(const QString &msg);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);
    void onHeartbeatTimer();

private:
    void sendJson(const QJsonObject &obj);
    void processMessage(const QJsonObject &msg);

    QTcpSocket m_socket;
    QTimer m_heartbeatTimer;
    QByteArray m_rxBuffer;

    int m_stationId = 1;
    QString m_stationName;
    QString m_hostIp;
    quint16 m_port = 9100;
    ReferenceMode m_referenceMode = ReferenceMode::LiveReference;
};

#endif // TCP_CLIENT_H
