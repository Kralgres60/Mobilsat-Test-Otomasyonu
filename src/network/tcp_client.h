/**
 * @file tcp_client.h
 * @brief Transport-agnostic station protocol client communicating with Central Host (Architecture 1).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include "core/data_models.h"
#include "net_stream.h"

/**
 * @class TcpClient
 * @brief Operates on each factory test station, connecting to the Host command endpoint
 *        via NetClientStream, maintaining keep-alive heartbeats, requesting reference locks,
 *        and receiving reference RF telemetry.
 */
class TcpClient : public QObject {
    Q_OBJECT
public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient() override;

    /** @brief Binds an external NetClientStream (e.g. custom transport or mock). */
    void setStream(NetClientStream *stream);

    /** @brief Returns the active NetClientStream used by this client. */
    NetClientStream* stream() const { return m_stream; }

    /** @brief Connects to Host command server. */
    void connectToHost(const QString &hostIp, quint16 port = 9100);

    /** @brief Disconnects from host. */
    void disconnectFromHost();

    /** @brief Checks connection state. */
    bool isConnected() const;

    /** @brief Registers station ID and name upon connecting. */
    void registerStation(int stationId, const QString &stationName);

    /** @brief Returns the active station ID assigned/confirmed by the Host server. */
    int assignedStationId() const { return m_stationId; }

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

    /** @brief Returns the active Golden Sample record ID designated by the Host. */
    int activeGoldenRecordId() const { return m_activeGoldenRecordId; }

signals:
    void connected();
    void disconnected();
    void registered();
    /** @brief Emitted when Host assigns/confirms the unique station ID for this workstation. */
    void stationIdAssigned(int stationId);
    void lockGranted(int timeoutSec);
    void lockWaiting(int queuePosition, int activeStationId);
    void lockExpired(const QString &reason);
    void refDataReceived(double refCn, double refPow, bool carrierLocked, bool videoLocked);
    /** @brief Emitted when Host updates or sends the active reference mode (Live vs Golden Sample). */
    void referenceModeChanged(ReferenceMode mode);
    /** @brief Emitted when Host gracefully shuts down the server. */
    void serverShutdown();
    /** @brief Emitted when Host returns or pushes live user authorization for the logged-in username. */
    void userAuthorizationReceived(const QString &role, const QString &fullName, bool isActive);
    void logMessage(const QString &msg);

private slots:
    void onConnected();
    void onDataReceived(const QByteArray &data);
    void onDisconnected();
    void onErrorOccurred(const QString &error);
    void onHeartbeatTimer();

private:
    void ensureDefaultStream();
    void attachStreamSignals(NetClientStream *stream);
    void sendJson(const QJsonObject &obj);
    void processMessage(const QJsonObject &msg);

    NetClientStream *m_stream = nullptr;
    NetClientStream *m_ownedStream = nullptr;
    QObject         *m_ownedTransport = nullptr;
    QTimer m_heartbeatTimer;
    QByteArray m_rxBuffer;

    int m_stationId = 1;
    QString m_stationName;
    QString m_hostIp;
    quint16 m_port = 9100;
    ReferenceMode m_referenceMode = ReferenceMode::LiveReference;
    int m_activeGoldenRecordId = 0;
};

#endif // TCP_CLIENT_H
