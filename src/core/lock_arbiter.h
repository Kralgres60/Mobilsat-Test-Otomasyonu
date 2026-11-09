/**
 * @file lock_arbiter.h
 * @brief FIFO Mutual Exclusion Arbiter for shared reference hardware (Architecture 1).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef LOCK_ARBITER_H
#define LOCK_ARBITER_H

#include <QObject>
#include <QQueue>
#include <QTimer>

/**
 * @class LockArbiter
 * @brief Arbitrates exclusive access to the shared Golden Sample Reference DRX9000
 *        among multiple factory test stations using a FIFO queue and safety timeout guards.
 */
class LockArbiter : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs the lock arbiter.
     * @param parent Parent QObject
     */
    explicit LockArbiter(QObject *parent = nullptr);

    /**
     * @brief Requests exclusive lock on the reference receiver.
     * @param stationId Requesting station identifier (1..3)
     * @param timeoutSec Safety guard timeout in seconds (default 90s)
     * @return true if lock was immediately acquired; false if queued
     */
    bool requestLock(int stationId, int timeoutSec = 90);

    /**
     * @brief Releases lock held by the specified station, processing the next queued station.
     * @param stationId Releasing station identifier
     */
    void releaseLock(int stationId);

    /**
     * @brief Cancels waiting status if station is queued.
     * @param stationId Station identifier to dequeue
     */
    void cancelWaiting(int stationId);

    /** @brief Checks if lock is currently held by any station. */
    bool isLocked() const { return m_activeStationId > 0; }

    /** @brief Returns active station ID holding the lock (or 0 if idle). */
    int activeStationId() const { return m_activeStationId; }

    /** @brief Returns 1-based queue position for the specified station (0 if not queued). */
    int queuePosition(int stationId) const;

    /** @brief Returns remaining time in seconds before the current lock expires. */
    int remainingSeconds() const;

signals:
    /** @brief Emitted when lock is granted to a station. */
    void lockGranted(int stationId, int timeoutSec);

    /** @brief Emitted when station request is placed in FIFO waiting queue. */
    void lockQueued(int stationId, int position, int currentActiveStationId);

    /** @brief Emitted when active lock is released by a station. */
    void lockReleased(int stationId);

    /** @brief Emitted when active lock expires due to safety guard timeout. */
    void lockExpired(int stationId);

private slots:
    void onTimeout();

private:
    void processNextInQueue();

    int m_activeStationId = 0;       ///< Station currently holding the lock (0 = available)
    int m_currentTimeoutSec = 90;    ///< Safety timeout duration in seconds
    QQueue<int> m_queue;             ///< FIFO waiting queue of station IDs
    QTimer m_guardTimer;             ///< Safety guard timer preventing deadlock
};

#endif // LOCK_ARBITER_H
