/**
 * @file tcp_server.h
 * @brief Central TCP command, lock arbitration, and telemetry broadcast server (Architecture 1).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QMap>
#include "data_models.h"
#include "lock_arbiter.h"
#include "hardware/serial_worker.h"
#include "hardware/drx_web_client.h"

/**
 * @class TcpServer
 * @brief Operates the central TCP communication endpoint on the Host PC (:9100),
 *        dispatching lock grants, queuing requests, and streaming Reference DRX telemetry
 *        (primary via Ethernet Web CGI, optional RS232 fallback).
 */
class TcpServer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs TCP command server.
     * @param arbiter FIFO mutual exclusion arbiter
     * @param refSerial Optional RS232 fallback worker for Golden Sample Reference DRX
     * @param parent Parent QObject
     */
    explicit TcpServer(LockArbiter *arbiter, SerialWorker *refSerial, QObject *parent = nullptr);
    ~TcpServer() override;

    /**
     * @brief Injects the primary Ethernet Web CGI client for the Reference Golden Sample DRX.
     * @param refWebClient Configured DrxWebClient targeting the Host Reference DRX IP
     */
    void setRefWebClient(DrxWebClient *refWebClient);

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
    bool isListening() const { return m_server.isListening(); }

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
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

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
    void processClientMessage(QTcpSocket *socket, const QJsonObject &msg);
    void sendToStation(int stationId, const QJsonObject &msg);
    QTcpSocket* findSocketByStationId(int stationId);

    QTcpServer m_server;
    LockArbiter *m_arbiter;
    SerialWorker *m_refSerial;
    DrxWebClient *m_refWebClient = nullptr;
    QTimer m_refPollTimer;
    QTimer m_silenceWatchdogTimer; ///< Detects and disconnects silent/unplugged stations
    ReferenceMode m_referenceMode = ReferenceMode::LiveReference;

    struct ClientSession {
        int stationId = 0;
        QString ipAddress;
        StationLiveStatus status;
        QByteArray rxBuffer;
    };

    QMap<QTcpSocket*, ClientSession> m_clients;

    // Cached live reference telemetry
    double m_lastRefCn = 0.0;
    double m_lastRefPow = 0.0;
    bool m_lastRefSLocked = false;
    bool m_lastRefVLocked = false;
    bool m_lastRefALocked = false;
};

#endif // TCP_SERVER_H
