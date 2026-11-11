/**
 * @file test_state_machine.h
 * @brief 18-step finite state machine coordinating transponder tuning, lock acquisition, and telemetry sampling.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TEST_STATE_MACHINE_H
#define TEST_STATE_MACHINE_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include "data_models.h"
#include "build_config.h"
#include "hardware/serial_worker.h"
#include "hardware/drx_web_client.h"
#include "network/tcp_client.h"

/**
 * @enum TestState
 * @brief Discrete operational states of the antenna automated test sequence.
 */
enum class TestState {
    Idle,               ///< Machine idle, awaiting operator start
    WaitingForLock,     ///< Awaiting mutual exclusion lock for Reference DRX (TCP or MariaDB)
    TuningChannel,      ///< Tuning DUT and Reference DRX to target transponder
    WaitingCarrierLock, ///< Waiting for RF carrier lock confirmation (S:+)
    SamplingTelemetry,  ///< Collecting 5-second multi-sample telemetry (250ms ticks)
    EvaluatingChannel,  ///< Computing average delta and evaluating pass/fail tolerance
    ReleasingLock,      ///< Yielding reference hardware lock to other stations
    Completed,          ///< All transponder channels tested successfully
    Aborted             ///< Test cancelled by operator or critical hardware failure
};

/**
 * @class TestStateMachine
 * @brief Coordinates the automated end-to-end RF test execution for an antenna.
 */
class TestStateMachine : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Constructs test state machine.
     * @param dutSerial Serial worker for DUT antenna (RS232 fallback)
     * @param netClient TCP client for host communication (Architecture 1)
     * @param parent Parent QObject
     */
    explicit TestStateMachine(SerialWorker *dutSerial, TcpClient *netClient, QObject *parent = nullptr);

    /**
     * @brief Injects Web CGI clients for DUT and Reference DRX9000 hardware.
     * @param dutWeb Web client connected to DUT antenna
     * @param refWeb Web client connected to Reference Golden Sample
     */
    void setWebClients(DrxWebClient *dutWeb, DrxWebClient *refWeb);

    /**
     * @brief Sets the active reference comparison mode dictated by the Administrator.
     * @param mode LiveReference (physical reference antenna) or GoldenSample (stored DB values)
     */
    void setReferenceMode(ReferenceMode mode) { m_referenceMode = mode; }

    /** @brief Returns the active reference comparison mode. */
    ReferenceMode referenceMode() const { return m_referenceMode; }

    /**
     * @brief Starts the automated multi-channel test sequence.
     * @param antennaSn Antenna serial number scanned from barcode
     * @param profile Satellite profile containing 10 transponders
     * @param workOrderId Production work order ID
     * @param operatorId Station operator ID
     * @return true if started successfully; false if already busy
     */
    bool startTest(const QString &antennaSn, const SatelliteProfileInfo &profile, int workOrderId = 0, int operatorId = 1);

    /**
     * @brief Aborts currently active test execution immediately.
     * @param reason Human-readable abort reason
     */
    void abortTest(const QString &reason = "Cancelled by operator");

    /** @brief Returns currently active state machine state. */
    TestState currentState() const { return m_state; }

    /** @brief Returns textual representation of current state. */
    QString currentStateString() const;

    /** @brief Returns active channel index (0..9). */
    int currentChannelIndex() const { return m_currentChannelIndex; }

    /** @brief Returns read-only reference to current test run data model. */
    const TestRunData& currentTestRun() const { return m_testRun; }

signals:
    /** @brief Emitted when finite state machine transitions to a new state. */
    void stateChanged(TestState newState, const QString &stateName);

    /** @brief Emitted when tuning commences for a new transponder channel. */
    void channelStarted(int channelNo, const QString &channelName, int freqMhz, const QString &pol);

    /** @brief Emitted on each 250ms sampling tick with updated telemetry. */
    void channelSampleUpdated(int channelNo, double dutCn, double dutPow, double refCn, double refPow, double currentDelta);

    /** @brief Emitted when a transponder channel finishes evaluation. */
    void channelCompleted(const ChannelSampleResult &result);

    /** @brief Emitted upon overall test completion with consolidated results. */
    void testFinished(const TestRunData &finalResult);

    /** @brief Emitted during waiting state with queue position info. */
    void waitingForLockUpdate(int queuePos, int activeStation);

    /** @brief Emitted for operator and developer logging. */
    void logMessage(const QString &msg);

private slots:
    void onLockGranted(int timeoutSec);
    void onLockWaiting(int queuePosition, int activeStationId);
    void onLockExpired(const QString &reason);

    void onDutStatus(bool signalLocked, bool videoProducing, bool audioProducing);
    void onDutTelemetry(double powerDbm, double cnDb, double ber);
    void onRefData(double refCn, double refPow, bool carrierLocked, bool videoLocked);

    void onCarrierTimeout();
    void onSampleTimerTick();
    void onSampleFinished();
    void onDbLockRetryTick();

private:
    void changeState(TestState newState);
    void runCurrentChannel();
    void finishCurrentChannel();
    void completeTest();

    SerialWorker *m_dutSerial;
    TcpClient *m_netClient;
    DrxWebClient *m_dutWebClient = nullptr;
    DrxWebClient *m_refWebClient = nullptr;

    TestState m_state = TestState::Idle;
    SatelliteProfileInfo m_profile;
    TestRunData m_testRun;
    int m_currentChannelIndex = 0; // 0..9

    // Timers
    QElapsedTimer m_totalTestTimer;
    QTimer m_carrierWaitTimer;      ///< Carrier acquisition timeout timer (15s)
    QTimer m_sampleDurationTimer;   ///< Sampling duration timer (5s)
    QTimer m_sampleTickTimer;       ///< 250ms multi-sampling collection ticker
    QTimer m_dbLockRetryTimer;      ///< MariaDB distributed lock retry timer (Architecture 2)

    // Per-channel sample accumulator pools
    QVector<double> m_dutCnSamples;
    QVector<double> m_dutPowSamples;
    QVector<double> m_refCnSamples;
    QVector<double> m_refPowSamples;

    bool m_dutCarrierLocked = false;
    bool m_dutVideoLocked = false;
    bool m_dutAudioLocked = false;

    bool m_refCarrierLocked = false;
    bool m_refVideoLocked = false;

    double m_lastRefCn = 0.0;
    double m_lastRefPow = 0.0;
    ReferenceMode m_referenceMode = ReferenceMode::LiveReference;
};

#endif // TEST_STATE_MACHINE_H
