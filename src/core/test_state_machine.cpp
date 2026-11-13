/**
 * @file test_state_machine.cpp
 * @brief Implementation of the 18-step antenna test execution state machine.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "test_state_machine.h"
#include "app_config.h"
#include "app_logger.h"
#include "database/db_manager.h"
#include <QDateTime>
#include <QtMath>
#include <numeric>

TestStateMachine::TestStateMachine(SerialWorker *dutSerial, TcpClient *netClient, QObject *parent)
    : QObject(parent), m_dutSerial(dutSerial), m_netClient(netClient) {
    
    m_carrierWaitTimer.setSingleShot(true);
    connect(&m_carrierWaitTimer, &QTimer::timeout, this, &TestStateMachine::onCarrierTimeout);

    m_sampleDurationTimer.setSingleShot(true);
    connect(&m_sampleDurationTimer, &QTimer::timeout, this, &TestStateMachine::onSampleFinished);

    connect(&m_sampleTickTimer, &QTimer::timeout, this, &TestStateMachine::onSampleTimerTick);

    connect(&m_dbLockRetryTimer, &QTimer::timeout, this, &TestStateMachine::onDbLockRetryTick);

    if (m_netClient) {
        connect(m_netClient, &TcpClient::lockGranted, this, &TestStateMachine::onLockGranted);
        connect(m_netClient, &TcpClient::lockWaiting, this, &TestStateMachine::onLockWaiting);
        connect(m_netClient, &TcpClient::lockExpired, this, &TestStateMachine::onLockExpired);
        connect(m_netClient, &TcpClient::refDataReceived, this, &TestStateMachine::onRefData);
    }

    if (m_dutSerial) {
        connect(m_dutSerial, &SerialWorker::statusReceived, this, &TestStateMachine::onDutStatus);
        connect(m_dutSerial, &SerialWorker::telemetryReceived, this, &TestStateMachine::onDutTelemetry);
    }
}

void TestStateMachine::setDataTransfers(DataTransfer *dutTransfer, DataTransfer *refTransfer) {
    m_dutTransfer = dutTransfer;
    m_refTransfer = refTransfer;

    if (m_dutTransfer) {
        connect(m_dutTransfer, &DataTransfer::statusReceived, this,
                [this](bool sig, bool vid, bool aud) {
                    onDutStatus(sig, vid, aud);
                });
        connect(m_dutTransfer, &DataTransfer::telemetryReceived, this,
                [this](double pwr, double cn, double ber) {
                    onDutTelemetry(pwr, cn, ber);
                });
    }

    if (m_refTransfer) {
        connect(m_refTransfer, &DataTransfer::statusReceived, this,
                [this](bool sig, bool vid, bool /*aud*/) {
                    m_refCarrierLocked = sig;
                    m_refVideoLocked = vid;
                });
        connect(m_refTransfer, &DataTransfer::telemetryReceived, this,
                [this](double pwr, double cn, double /*ber*/) {
                    onRefData(cn, pwr, m_refCarrierLocked, m_refVideoLocked);
                });
    }
}

void TestStateMachine::setWebClients(DrxWebClient *dutWeb, DrxWebClient *refWeb) {
    m_dutWebClient = dutWeb;
    m_refWebClient = refWeb;
    setDataTransfers(dutWeb, refWeb);
}

QString TestStateMachine::currentStateString() const {
    switch (m_state) {
        case TestState::Idle: return "BEKLEMEDE";
        case TestState::WaitingForLock: return "REFERANS KİLİDİ BEKLENİYOR";
        case TestState::TuningChannel: return "FREKANS AYARLANIYOR";
        case TestState::WaitingCarrierLock: return "SİNYAL KİLİDİ BEKLENİYOR";
        case TestState::SamplingTelemetry: return "VERİ ÖRNEKLENİYOR";
        case TestState::EvaluatingChannel: return "KANAL HESAPLANIYOR";
        case TestState::ReleasingLock: return "KİLİT BIRAKILIYOR";
        case TestState::Completed: return "TEST TAMAMLANDI";
        case TestState::Aborted: return "İPTAL EDİLDİ";
    }
    return "";
}

void TestStateMachine::changeState(TestState newState) {
    m_state = newState;
    emit stateChanged(newState, currentStateString());
}

bool TestStateMachine::startTest(const QString &antennaSn,
                                 const SatelliteProfileInfo &profile,
                                 int workOrderId,
                                 int operatorId,
                                 const QString &testerName) {
    if (m_state != TestState::Idle && m_state != TestState::Completed && m_state != TestState::Aborted) {
        emit logMessage("Test is already running!");
        return false;
    }

    if (testerName.trimmed().isEmpty()) {
        emit logMessage("Test başlatılamadı: Testi yapan kişi adı girilmesi zorunludur!");
        return false;
    }

    if (profile.channels.isEmpty()) {
        emit logMessage("Selected satellite profile contains no transponder channels!");
        return false;
    }

    m_profile = profile;
    m_testRun = TestRunData();
    m_testRun.antennaSn = antennaSn;
    m_testRun.workOrderId = workOrderId;
    m_testRun.operatorId = operatorId;
    m_testRun.testerName = testerName.trimmed();
    m_testRun.operatorUsername = AppConfig::instance().currentUsername();
    m_testRun.satelliteProfileId = profile.id;
    m_testRun.profileName = profile.profileName;
    m_testRun.stationId = AppConfig::instance().stationId();
    m_testRun.timestamp = QDateTime::currentDateTime();
    m_testRun.overallResult = "IN_PROGRESS";

    m_currentChannelIndex = 0;
    m_totalTestTimer.restart();

    emit logMessage(QString("Starting Automated Test: Tester=%1, Serial Number=%2, Profile=%3 (%4 Channels)")
                        .arg(m_testRun.testerName, antennaSn, profile.profileName).arg(profile.channels.size()));
    AppLogger::instance().logProcess(
        QString("ST-%1").arg(m_testRun.stationId),
        "TEST_START",
        QString("Tester=%1 | SN=%2 | Profile=%3 | Channels=%4")
            .arg(m_testRun.testerName, antennaSn, profile.profileName).arg(profile.channels.size()));

    changeState(TestState::WaitingForLock);

#if defined(NETA_ALGORITMA_2_THREE_PC)
    int lockedBy = 0, curCh = 0;
    int myStationId = AppConfig::instance().stationId();
    if (DbManager::instance().tryAcquireDbLock(myStationId, lockedBy, curCh)) {
        emit logMessage(QString("[Algorithm 2] Reference lock successfully acquired from MariaDB (Station %1).").arg(myStationId));
        onLockGranted(AppConfig::instance().lockTimeoutSec());
    } else {
        emit waitingForLockUpdate(1, lockedBy);
        emit logMessage(QString("[Algorithm 2] Reference DRX currently in use by Station %1 (Channel %2). Enqueued...").arg(lockedBy).arg(curCh));
        m_dbLockRetryTimer.start(1000); // Poll lock table every second
    }
#else
    // Algorithm 1: Request reference hardware lock over TCP from Central Host
    if (m_netClient) {
        m_netClient->requestLock(AppConfig::instance().lockTimeoutSec());
    } else {
        // Fallback for standalone / single-station testing
        onLockGranted(90);
    }
#endif

    return true;
}

void TestStateMachine::onDbLockRetryTick() {
#if defined(NETA_ALGORITMA_2_THREE_PC)
    if (m_state != TestState::WaitingForLock) {
        m_dbLockRetryTimer.stop();
        return;
    }

    int lockedBy = 0, curCh = 0;
    int myStationId = AppConfig::instance().stationId();
    if (DbManager::instance().tryAcquireDbLock(myStationId, lockedBy, curCh)) {
        m_dbLockRetryTimer.stop();
        emit logMessage(QString("[Algorithm 2] Reference lock vacated and acquired by Station %1!").arg(myStationId));
        onLockGranted(AppConfig::instance().lockTimeoutSec());
    } else {
        emit waitingForLockUpdate(1, lockedBy);
    }
#endif
}

void TestStateMachine::abortTest(const QString &reason) {
    m_carrierWaitTimer.stop();
    m_sampleDurationTimer.stop();
    m_sampleTickTimer.stop();
    m_dbLockRetryTimer.stop();

    if (m_dutSerial) {
        m_dutSerial->stopTunerLog();
    }

#if defined(NETA_ALGORITMA_2_THREE_PC)
    DbManager::instance().releaseDbLock(AppConfig::instance().stationId());
    emit logMessage(QString("[Algorithm 2] MariaDB reference lock released (Station %1).").arg(AppConfig::instance().stationId()));
#else
    if (m_netClient) {
        m_netClient->releaseLock();
    }
#endif

    m_testRun.overallResult = "ABORTED";
    m_testRun.failReason = reason;
    m_testRun.durationSec = m_totalTestTimer.elapsed() / 1000;

    changeState(TestState::Aborted);
    emit testFinished(m_testRun);
    emit logMessage("Test aborted: " + reason);
}

void TestStateMachine::onLockGranted(int /*timeoutSec*/) {
    if (m_state != TestState::WaitingForLock) return;
    emit logMessage("Reference hardware lock granted. Commencing multi-channel testing pipeline...");
    runCurrentChannel();
}

void TestStateMachine::onLockWaiting(int queuePosition, int activeStationId) {
    emit waitingForLockUpdate(queuePosition, activeStationId);
}

void TestStateMachine::onLockExpired(const QString &reason) {
    abortTest("Lock guard timeout expired: " + reason);
}

void TestStateMachine::runCurrentChannel() {
    if (m_currentChannelIndex >= m_profile.channels.size()) {
        completeTest();
        return;
    }

    const auto &ch = m_profile.channels[m_currentChannelIndex];
    changeState(TestState::TuningChannel);

    emit channelStarted(ch.channelNo, ch.channelName, ch.frequencyMhz, ch.polarization);
    emit logMessage(QString("[%1/%2] Tuning %3 (%4 MHz, %5 kS/s, %6)...")
                        .arg(m_currentChannelIndex + 1)
                        .arg(m_profile.channels.size())
                        .arg(ch.channelName)
                        .arg(ch.frequencyMhz)
                        .arg(ch.symbolRate)
                        .arg(ch.polarization));

    // Reset channel state and sample buffers
    m_dutCarrierLocked = false;
    m_dutVideoLocked = false;
    m_dutAudioLocked = false;
    m_dutCnSamples.clear();
    m_dutPowSamples.clear();
    m_refCnSamples.clear();
    m_refPowSamples.clear();

    // 1. Send tune command to DUT DRX9000 (DataTransfer abstraction first, fallback to direct pointers)
    if (m_dutTransfer) {
        m_dutTransfer->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
    } else {
        if (m_dutWebClient) {
            m_dutWebClient->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        }
        if (m_dutSerial && m_dutSerial->isConnected()) {
            m_dutSerial->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        }
    }

    // 2. Reference comparison handling:
    //    - GoldenSample: look up Host-measured golden_frequencies by (freq, sr, pol) or channel fallback
    //    - LiveReference: tune shared Reference DRX (via Host TCP in Alg 1, or direct LAN in Alg 2)
    if (m_referenceMode == ReferenceMode::GoldenSample) {
        double gCn = ch.goldenCnDb;
        double gPow = ch.goldenPowDbm;
        double gTol = ch.toleranceDb;
        bool foundInPool = DbManager::instance().findGoldenSampleForChannel(
            ch.frequencyMhz, ch.symbolRate, ch.polarization, gCn, gPow, gTol);
        bool hasBaseline = foundInPool || ch.hasGoldenSample;

        m_lastRefCn = gCn;
        m_lastRefPow = gPow;
        if (foundInPool && gTol > 0.05) {
            m_profile.channels[m_currentChannelIndex].toleranceDb = gTol;
        }
        m_refCarrierLocked = true;
        m_refVideoLocked = true;
        m_refCnSamples.append(gCn);
        m_refPowSamples.append(gPow);
        emit logMessage(QString("[Golden Sample] Kayıtlı frekans referansı kullanılıyor (%1 - %2 MHz %3): C/N=%4 dB | POW=%5 dBm%6")
                            .arg(ch.channelName)
                            .arg(ch.frequencyMhz)
                            .arg(ch.polarization)
                            .arg(gCn, 0, 'f', 2)
                            .arg(gPow, 0, 'f', 2)
                            .arg(hasBaseline ? "" : " [UYARI: Yönetici bu frekans için Golden ölçüm kaydetmemiş!]"));
    } else {
#if defined(NETA_ALGORITMA_2_THREE_PC)
        // Algorithm 2: Update MariaDB lock heartbeat and tune shared Reference DRX9000 via Web CGI
        DbManager::instance().updateDbLockHeartbeat(AppConfig::instance().stationId(), ch.channelNo);
        if (m_refTransfer) {
            m_refTransfer->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        } else if (m_refWebClient) {
            m_refWebClient->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        }
#else
        // Algorithm 1: Request Host to tune Golden Sample Reference DRX (TCP :9100)
        if (m_netClient) {
            m_netClient->requestRefTune(ch.channelNo, ch.frequencyMhz, ch.symbolRate, ch.polarization);
        }
#endif
    }

    changeState(TestState::WaitingCarrierLock);
    m_carrierWaitTimer.start(6000); // Allow up to 6 seconds for Carrier Lock (S:+)
    m_sampleTickTimer.start(450);   // Poll Web CGI alarmStatus every 450ms until carrier locks
}

void TestStateMachine::onDutStatus(bool signalLocked, bool videoProducing, bool audioProducing) {
    m_dutCarrierLocked = signalLocked;
    m_dutVideoLocked = videoProducing;
    m_dutAudioLocked = audioProducing;

    if (m_state == TestState::WaitingCarrierLock && signalLocked) {
        // Carrier lock acquired; transition to sampling
        m_carrierWaitTimer.stop();
        changeState(TestState::SamplingTelemetry);

        if (m_dutSerial && m_dutSerial->isConnected()) {
            m_dutSerial->startTunerLog();
        }

        int sampleSec = AppConfig::instance().sampleDurationSec();
        m_sampleDurationTimer.start(sampleSec * 1000);
        m_sampleTickTimer.start(250); // Sample telemetry at 250ms intervals
        emit logMessage(QString("Carrier locked (S:+). Collecting telemetry samples for %1 seconds...").arg(sampleSec));
    }
}

void TestStateMachine::onCarrierTimeout() {
    if (m_state == TestState::WaitingCarrierLock) {
        m_sampleTickTimer.stop();
        emit logMessage("Carrier lock timeout (S:-)! Channel marked as FAIL.");
        finishCurrentChannel();
    }
}

void TestStateMachine::onDutTelemetry(double powerDbm, double cnDb, double /*ber*/) {
    if (m_state == TestState::SamplingTelemetry) {
        m_dutPowSamples.append(powerDbm);
        m_dutCnSamples.append(cnDb);

        double delta = cnDb - m_lastRefCn;
        const auto &ch = m_profile.channels[m_currentChannelIndex];
        emit channelSampleUpdated(ch.channelNo, cnDb, powerDbm, m_lastRefCn, m_lastRefPow, delta);
    }
}

void TestStateMachine::onRefData(double refCn, double refPow, bool carrierLocked, bool videoLocked) {
    if (m_referenceMode == ReferenceMode::GoldenSample) {
        return; // Keep stored Golden Sample baseline unchanged
    }
    m_lastRefCn = refCn;
    m_lastRefPow = refPow;
    m_refCarrierLocked = carrierLocked;
    m_refVideoLocked = videoLocked;

    if (m_state == TestState::SamplingTelemetry) {
        m_refCnSamples.append(refCn);
        m_refPowSamples.append(refPow);
    }
}

void TestStateMachine::onSampleTimerTick() {
    if (m_dutTransfer) {
        m_dutTransfer->requestStatus();
    } else {
        if (m_dutWebClient) {
            m_dutWebClient->requestStatus();
        }
        if (m_dutSerial && m_dutSerial->isConnected()) {
            m_dutSerial->requestStatus();
        }
    }
#if defined(NETA_ALGORITMA_2_THREE_PC)
    if (m_referenceMode == ReferenceMode::LiveReference) {
        if (m_refTransfer) {
            m_refTransfer->requestStatus();
        } else if (m_refWebClient) {
            m_refWebClient->requestStatus();
        }
    }
#endif
}

void TestStateMachine::onSampleFinished() {
    m_sampleTickTimer.stop();
    if (m_dutSerial) {
        m_dutSerial->stopTunerLog();
    }
    finishCurrentChannel();
}

void TestStateMachine::finishCurrentChannel() {
    changeState(TestState::EvaluatingChannel);

    const auto &ch = m_profile.channels[m_currentChannelIndex];
    ChannelSampleResult res;
    res.channelNo = ch.channelNo;
    res.channelName = ch.channelName;
    res.frequencyMhz = ch.frequencyMhz;
    res.symbolRate = ch.symbolRate;
    res.polarization = ch.polarization;
    res.toleranceDb = ch.toleranceDb;
    res.carrierLocked = m_dutCarrierLocked;
    res.videoLocked = m_dutVideoLocked;
    res.audioLocked = m_dutAudioLocked;

    // Compute DUT arithmetic averages
    if (!m_dutCnSamples.isEmpty()) {
        double sumCn = std::accumulate(m_dutCnSamples.begin(), m_dutCnSamples.end(), 0.0);
        double sumPow = std::accumulate(m_dutPowSamples.begin(), m_dutPowSamples.end(), 0.0);
        res.dutCn = sumCn / m_dutCnSamples.size();
        res.dutPow = sumPow / m_dutPowSamples.size();
    }

    // Compute REF arithmetic averages
    if (!m_refCnSamples.isEmpty()) {
        double sumRefCn = std::accumulate(m_refCnSamples.begin(), m_refCnSamples.end(), 0.0);
        double sumRefPow = std::accumulate(m_refPowSamples.begin(), m_refPowSamples.end(), 0.0);
        res.refCn = sumRefCn / m_refCnSamples.size();
        res.refPow = sumRefPow / m_refPowSamples.size();
    } else {
        res.refCn = m_lastRefCn;
        res.refPow = m_lastRefPow;
    }

    res.deltaDb = res.dutCn - res.refCn;

    // Tolerance Pass/Fail check
    if (!res.carrierLocked) {
        res.status = "FAIL";    // Carrier signal unlock failure
    } else if (qAbs(res.deltaDb) > res.toleranceDb) {
        res.status = "FAIL";    // Exceeded attenuation tolerance
    } else if (!res.videoLocked) {
        res.status = "WARNING"; // Delta OK, but video locked flag missing
    } else {
        res.status = "PASS";    // Passed completely
    }

    m_testRun.channels.append(res);
    emit channelCompleted(res);

    AppLogger::instance().logProcess(
        QString("ST-%1").arg(m_testRun.stationId),
        "CHANNEL_RESULT",
        QString("Ch=%1 (%2 MHz %3) | DUT=%4 dB | REF=%5 dB | Delta=%6 dB | Status=%7")
            .arg(res.channelName)
            .arg(res.frequencyMhz)
            .arg(res.polarization)
            .arg(res.dutCn, 0, 'f', 2)
            .arg(res.refCn, 0, 'f', 2)
            .arg(res.deltaDb, 0, 'f', 2)
            .arg(res.status));

    emit logMessage(QString("[%1/%2] %3 Evaluated: DUT=%4 dB, REF=%5 dB, Delta=%6 dB -> %7")
                        .arg(m_currentChannelIndex + 1)
                        .arg(m_profile.channels.size())
                        .arg(res.channelName)
                        .arg(res.dutCn, 0, 'f', 1)
                        .arg(res.refCn, 0, 'f', 1)
                        .arg(res.deltaDb, 0, 'f', 1)
                        .arg(res.status));

    // Transition to next transponder
    m_currentChannelIndex++;
    if (m_currentChannelIndex < m_profile.channels.size()) {
        runCurrentChannel();
    } else {
        completeTest();
    }
}

void TestStateMachine::completeTest() {
    changeState(TestState::ReleasingLock);

#if defined(NETA_ALGORITMA_2_THREE_PC)
    m_dbLockRetryTimer.stop();
    DbManager::instance().releaseDbLock(AppConfig::instance().stationId());
    emit logMessage(QString("[Algorithm 2] Test complete. MariaDB lock released (Station %1).").arg(AppConfig::instance().stationId()));
#else
    // Release Reference hardware lock (Algorithm 1)
    if (m_netClient) {
        m_netClient->releaseLock();
    }
#endif

    m_testRun.durationSec = m_totalTestTimer.elapsed() / 1000;

    // Overall verdict calculation
    bool hasFail = false;
    double sumDelta = 0.0;
    double maxDelta = 0.0;

    for (const auto &ch : m_testRun.channels) {
        if (ch.status == "FAIL") {
            hasFail = true;
            if (m_testRun.failReason.isEmpty()) {
                m_testRun.failReason = QString("Channel %1 (%2) failed (Delta: %3 dB, Tol: %4 dB)")
                                           .arg(ch.channelNo).arg(ch.channelName)
                                           .arg(ch.deltaDb, 0, 'f', 2).arg(ch.toleranceDb, 0, 'f', 2);
            }
        }
        sumDelta += ch.deltaDb;
        if (qAbs(ch.deltaDb) > maxDelta) {
            maxDelta = qAbs(ch.deltaDb);
        }
    }

    if (!m_testRun.channels.isEmpty()) {
        m_testRun.avgDeltaDb = sumDelta / m_testRun.channels.size();
    }
    m_testRun.maxDeltaDb = maxDelta;
    m_testRun.overallResult = hasFail ? "FAIL" : "PASS";

    changeState(TestState::Completed);
    emit testFinished(m_testRun);

    AppLogger::instance().logProcess(
        QString("ST-%1").arg(m_testRun.stationId),
        "TEST_FINISH",
        QString("SN=%1 | Tester=%2 | Verdict=%3 | Duration=%4s | AvgDelta=%5 dB | MaxDelta=%6 dB")
            .arg(m_testRun.antennaSn, m_testRun.testerName, m_testRun.overallResult)
            .arg(m_testRun.durationSec)
            .arg(m_testRun.avgDeltaDb, 0, 'f', 2)
            .arg(m_testRun.maxDeltaDb, 0, 'f', 2));

    emit logMessage(QString("=== TEST RUN COMPLETE: VERDICT: %1 (Duration: %2 s, Avg Delta: %3 dB) ===")
                        .arg(m_testRun.overallResult)
                        .arg(m_testRun.durationSec)
                        .arg(m_testRun.avgDeltaDb, 0, 'f', 2));
}
