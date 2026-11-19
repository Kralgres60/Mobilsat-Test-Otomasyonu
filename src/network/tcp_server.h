/**
 * @file tcp_server.h
 * @brief Transport-agnostic central command, lock arbitration, and telemetry broadcast server (Architecture 1).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QTimer>
#include "data_models.h"
#include "lock_arbiter.h"
#include "net_stream.h"
#include "hardware/data_stream.h"

/**
 * @class TcpServer
 * @brief Operates the central command endpoint on the Host PC (:9100) via NetServerStream,
 *        dispatching lock grants, queuing requests, and streaming Reference DRX telemetry
 *        through the hardware-agnostic DataStream abstraction.
 */
class TcpServer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs TCP command server.
     * @param arbiter FIFO mutual exclusion arbiter
     * @param refStream Hardware-agnostic DataStream for Reference DRX (optional, can be set later)
     * @param parent Parent QObject
     */
    explicit TcpServer(LockArbiter *arbiter, DataStream *refStream = nullptr, QObject *parent = nullptr);
    ~TcpServer() override;

    /** @brief Binds an external NetServerStream (e.g. custom transport or mock). */
    void setStream(NetServerStream *stream);

    /** @brief Injects the hardware-agnostic DataStream for the Reference Golden Sample DRX. */
    void setRefDataStream(DataStream *refStream);

    /**
     * @brief Sets the active reference comparison mode and broadcasts it to connected stations.
     * @param mode ReferenceMode::LiveReference or ReferenceMode::GoldenSample
     */
    void setReferenceMode(ReferenceMode mode);

    /** @brief Returns the currently active reference comparison mode. */
    ReferenceMode referenceMode() const { return m_referenceMode; }

    /** @brief Binds and starts listening for client station connections. */
    bool start(quint16 port = 9100);

    /** @brief Stops server and disconnects connected stations. */
    void stop();

    /** @brief Checks if server is actively listening. */
    bool isListening() const;

    /** @brief Returns live status dictionary of connected test stations. */
    QMap<int, StationLiveStatus> connectedStations() const;

    /** @brief Broadcasts updated user role/active authorization records to connected stations. */
    void broadcastUserRoleUpdates();

signals:
    void stationConnected(int stationId, const QString &ip);
    void stationDisconnected(int stationId);
    void stationStatusUpdated(const StationLiveStatus &status);
    void logMessage(const QString &msg);

private slots:
    void onPeerConnected(quint64 peerId, const QString &ipAddress);
    void onPeerDataReceived(quint64 peerId, const QByteArray &data);
    void onPeerDisconnected(quint64 peerId);

    // Lock Arbiter event handlers
    void onLockGranted(int stationId, int timeoutSec);
    void onLockQueued(int stationId, int position, int activeStationId);
    void onLockReleased(int stationId);
    void onLockExpired(int stationId);

    // Reference receiver polling and telemetry listeners
    void onRefPollTick();
    void onRefTelemetry(double powDbm, double cnDb, double ber);
    void onRefStatus(bool sLocked, bool vLocked, bool aLocked);
    /** @brief Drops stations that have gone silent past the heartbeat timeout. */
    void onSilenceWatchdogTick();

private:
    void ensureDefaultStream();
    void attachStreamSignals(NetServerStream *stream);
    void processClientMessage(quint64 peerId, const QJsonObject &msg);
    void sendToStation(int stationId, const QJsonObject &msg);
    void broadcastJson(const QJsonObject &msg);
    quint64 findPeerByStationId(int stationId) const;

    NetServerStream *m_stream = nullptr;
    NetServerStream *m_ownedStream = nullptr;
    QObject         *m_ownedTransport = nullptr;

    LockArbiter *m_arbiter = nullptr;
    DataStream  *m_refStream = nullptr;
    QTimer m_refPollTimer;
    QTimer m_silenceWatchdogTimer; ///< Detects and disconnects silent/unplugged stations
    ReferenceMode m_referenceMode = ReferenceMode::LiveReference;

    struct ClientSession {
        int stationId = 0;
        QString ipAddress;
        StationLiveStatus status;
        QByteArray rxBuffer;
    };

    QMap<quint64, ClientSession> m_clients;

    // Cached live reference telemetry
    double m_lastRefCn = 0.0;
    double m_lastRefPow = 0.0;
    bool m_lastRefSLocked = false;
    bool m_lastRefVLocked = false;
    bool m_lastRefALocked = false;
};

#endif // TCP_SERVER_H
