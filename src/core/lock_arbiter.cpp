/**
 * @file lock_arbiter.cpp
 * @brief Implementation of the FIFO Reference Lock Arbiter.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "lock_arbiter.h"
#include <QDebug>

LockArbiter::LockArbiter(QObject *parent) : QObject(parent) {
    m_guardTimer.setSingleShot(true);
    connect(&m_guardTimer, &QTimer::timeout, this, &LockArbiter::onTimeout);
}

bool LockArbiter::requestLock(int stationId, int timeoutSec) {
    if (m_activeStationId == stationId) {
        // Lock already held by this station; refresh guard timer
        m_currentTimeoutSec = timeoutSec;
        m_guardTimer.start(timeoutSec * 1000);
        emit lockGranted(stationId, timeoutSec);
        return true;
    }

    if (m_activeStationId == 0) {
        // Reference hardware is idle; grant lock immediately
        m_activeStationId = stationId;
        m_currentTimeoutSec = timeoutSec;
        m_guardTimer.start(timeoutSec * 1000);
        qDebug() << "[LockArbiter] Lock granted to Station" << stationId << "(" << timeoutSec << "sec)";
        emit lockGranted(stationId, timeoutSec);
        return true;
    }

    // Hardware busy by another station; enqueue request
    if (!m_queue.contains(stationId)) {
        m_queue.enqueue(stationId);
    }
    int pos = queuePosition(stationId);
    qDebug() << "[LockArbiter] Station" << stationId << "enqueued. Position:" << pos << "Active:" << m_activeStationId;
    emit lockQueued(stationId, pos, m_activeStationId);
    return false;
}

void LockArbiter::releaseLock(int stationId) {
    if (m_activeStationId == stationId) {
        qDebug() << "[LockArbiter] Lock released by Station" << stationId;
        m_guardTimer.stop();
        m_activeStationId = 0;
        emit lockReleased(stationId);

        // Process next awaiting station in FIFO queue
        processNextInQueue();
    } else {
        cancelWaiting(stationId);
    }
}

void LockArbiter::cancelWaiting(int stationId) {
    m_queue.removeAll(stationId);
}

int LockArbiter::queuePosition(int stationId) const {
    int idx = m_queue.indexOf(stationId);
    return (idx >= 0) ? (idx + 1) : 0;
}

int LockArbiter::remainingSeconds() const {
    if (!m_guardTimer.isActive()) return 0;
    return m_guardTimer.remainingTime() / 1000;
}

void LockArbiter::onTimeout() {
    if (m_activeStationId > 0) {
        int expiredStation = m_activeStationId;
        qWarning() << "[LockArbiter] SAFETY TIMEOUT EXPIRED! Dropping lock for Station" << expiredStation;
        m_activeStationId = 0;
        emit lockExpired(expiredStation);

        processNextInQueue();
    }
}

void LockArbiter::processNextInQueue() {
    while (!m_queue.isEmpty()) {
        int nextStation = m_queue.dequeue();
        if (nextStation > 0) {
            m_activeStationId = nextStation;
            m_guardTimer.start(m_currentTimeoutSec * 1000);
            qDebug() << "[LockArbiter] Lock granted to next queued Station" << nextStation;
            emit lockGranted(nextStation, m_currentTimeoutSec);
            return;
        }
    }
}
