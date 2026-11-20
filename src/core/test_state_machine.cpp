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

TestStateMachine::TestStateMachine(TcpClient *netClient, QObject *parent)
    : QObject(parent), m_netClient(netClient) {
    
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
}

void TestStateMachine::setDataStreams(DataStream *dutStream, DataStream *refStream) {
    m_dutStream = dutStream;
    m_refStream = refStream;

    if (m_dutStream) {
        connect(m_dutStream, &DataStream::statusReceived, this,
                [this](bool sig, bool vid, bool aud) {
                    onDutStatus(sig, vid, aud);
                });
        connect(m_dutStream, &DataStream::telemetryReceived, this,
                [this](double pwr, double cn, double ber) {
                    onDutTelemetry(pwr, cn, ber);
                });
        connect(m_dutStream, &DataStream::timestampReceived, this,
                [this](const QDateTime &devTime, const QDateTime &hTime, const QString &rawStr) {
                    onDutTimestampReceived(devTime, hTime, rawStr);
                });
    }

    if (m_refStream) {
        connect(m_refStream, &DataStream::statusReceived, this,
                [this](bool sig, bool vid, bool /*aud*/) {
                    m_refCarrierLocked = sig;
                    m_refVideoLocked = vid;
                });
        connect(m_refStream, &DataStream::telemetryReceived, this,
                [this](double pwr, double cn, double /*ber*/) {
                    onRefData(cn, pwr, m_refCarrierLocked, m_refVideoLocked);
                });
    }
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
                                 const QString &testerName,
                                 ReferenceMode refMode,
                                 const GoldenFrequencyRecord &selectedGolden) {
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

    m_referenceMode = refMode;
    m_selectedGoldenRecord = selectedGolden;
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

    QString modeDesc = (m_referenceMode == ReferenceMode::GoldenSample)
        ? QString("Kayıtlı Golden Test ('%1' - %2)")
              .arg(m_selectedGoldenRecord.label.isEmpty() ? "Varsayılan" : m_selectedGoldenRecord.label)
              .arg(m_selectedGoldenRecord.durationSec > 0 ? QString("%1 sn Canlı Kayıt").arg(m_selectedGoldenRecord.durationSec) : "Sabit Eşik")
        : "Canlı Referans Anten Testi";

    emit logMessage(QString("Starting Automated Test: Tester=%1, Serial Number=%2, Profile=%3 (%4 Channels), Mod=%5")
                        .arg(m_testRun.testerName, antennaSn, profile.profileName).arg(profile.channels.size()).arg(modeDesc));
    AppLogger::instance().logProcess(
        QString("ST-%1").arg(m_testRun.stationId),
        "TEST_START",
        QString("Tester=%1 | SN=%2 | Profile=%3 | Mode=%4")
            .arg(m_testRun.testerName, antennaSn, profile.profileName, modeDesc));

    bool needsLiveLock = false;
    if (m_referenceMode == ReferenceMode::LiveReference) {
        needsLiveLock = true;
    } else {
        // In Golden Sample mode, check if any profile transponder channel lacks a Golden Sample baseline
        for (const auto &ch : profile.channels) {
            bool hasGolden = false;
            if (m_selectedGoldenRecord.id > 0 && m_selectedGoldenRecord.isActive &&
                m_selectedGoldenRecord.frequencyMhz == ch.frequencyMhz &&
                (m_selectedGoldenRecord.polarization.isEmpty() || m_selectedGoldenRecord.polarization == ch.polarization)) {
                hasGolden = true;
            } else if (ch.hasGoldenSample && ch.goldenCnDb > 0.05) {
                hasGolden = true;
            } else {
                double gCn = 0.0, gPow = 0.0, gTol = 0.0;
                if (DbManager::instance().findGoldenSampleForChannel(ch.frequencyMhz, ch.symbolRate, ch.polarization, gCn, gPow, gTol)) {
                    hasGolden = true;
                }
            }
            if (!hasGolden) {
                needsLiveLock = true;
                break;
            }
        }
    }

    if (!needsLiveLock) {
        emit logMessage(QString("[Kayıtlı Test] Tüm frekanslar için Golden Sample mevcut. Donanımsal anten kilidi aranmaz."));
        changeState(TestState::WaitingForLock);
        onLockGranted(90);
    } else {
        if (m_referenceMode == ReferenceMode::GoldenSample) {
            emit logMessage(QString("[Hibrit Referans] Bazı frekanslar için Golden kayıt bulunamadığından Canlı Referans Anten kilidi talep ediliyor..."));
        }
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
    }

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

    if (m_referenceMode != ReferenceMode::GoldenSample) {
#if defined(NETA_ALGORITMA_2_THREE_PC)
        DbManager::instance().releaseDbLock(AppConfig::instance().stationId());
        emit logMessage(QString("[Algorithm 2] MariaDB reference lock released (Station %1).").arg(AppConfig::instance().stationId()));
#else
        if (m_netClient) {
            m_netClient->releaseLock();
        }
#endif
    }

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
    m_channelTimeHistory.clear();

    // 1. Send tune command to DUT DRX9000 via DataStream HAL
    if (m_dutStream) {
        m_dutStream->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
    }

    // 2. Reference comparison handling:
    //    Determine whether this transponder frequency has a Golden Sample baseline
    m_currentChannelIsGolden = false;
    double gCn = 0.0;
    double gPow = 0.0;
    double gTol = ch.toleranceDb;
    QString goldenLabel;

    if (m_referenceMode == ReferenceMode::GoldenSample) {
        // Priority 1: Check selected Golden Record matching this frequency & polarization (must be active)
        if (m_selectedGoldenRecord.id > 0 && m_selectedGoldenRecord.isActive &&
            m_selectedGoldenRecord.frequencyMhz == ch.frequencyMhz &&
            (m_selectedGoldenRecord.polarization.isEmpty() || m_selectedGoldenRecord.polarization == ch.polarization)) {
            gCn = m_selectedGoldenRecord.goldenCnDb;
            gPow = m_selectedGoldenRecord.goldenPowDbm;
            if (m_selectedGoldenRecord.toleranceDb > 0.05) gTol = m_selectedGoldenRecord.toleranceDb;
            goldenLabel = m_selectedGoldenRecord.label.isEmpty() ? QString("%1 MHz %2").arg(ch.frequencyMhz).arg(ch.polarization) : m_selectedGoldenRecord.label;
            m_currentChannelIsGolden = true;
        }
        // Priority 2: Check profile channel fixed golden sample
        else if (ch.hasGoldenSample && ch.goldenCnDb > 0.05) {
            gCn = ch.goldenCnDb;
            gPow = ch.goldenPowDbm;
            gTol = ch.toleranceDb;
            goldenLabel = QString("Profil Sabiti (%1)").arg(ch.channelName);
            m_currentChannelIsGolden = true;
        }
        // Priority 3: Check database golden_frequencies table for matching frequency & polarization
        else {
            bool foundInPool = DbManager::instance().findGoldenSampleForChannel(
                ch.frequencyMhz, ch.symbolRate, ch.polarization, gCn, gPow, gTol);
            if (foundInPool) {
                goldenLabel = QString("DB Kayıtlı (%1 MHz %2)").arg(ch.frequencyMhz).arg(ch.polarization);
                m_currentChannelIsGolden = true;
            }
        }
    }

    if (m_currentChannelIsGolden) {
        m_lastRefCn = gCn;
        m_lastRefPow = gPow;
        m_profile.channels[m_currentChannelIndex].toleranceDb = gTol;
        m_refCarrierLocked = true;
        m_refVideoLocked = true;
        m_refCnSamples.append(gCn);
        m_refPowSamples.append(gPow);
        emit logMessage(QString("[Kayıtlı Test] Frekans: %1 MHz %2 (%3) -> Kayıtlı Golden Sample kullanılıyor: C/N=%4 dB | POW=%5 dBm | Tolerans=±%6 dB (%7)")
                            .arg(ch.frequencyMhz)
                            .arg(ch.polarization)
                            .arg(ch.channelName)
                            .arg(gCn, 0, 'f', 2)
                            .arg(gPow, 0, 'f', 2)
                            .arg(gTol, 0, 'f', 2)
                            .arg(goldenLabel));
    } else {
        // Fallback to Live Reference DRX measurement (either mode is LiveReference or no golden baseline exists for this frequency)
        if (m_referenceMode == ReferenceMode::GoldenSample) {
            emit logMessage(QString("[Hibrit Referans] Frekans: %1 MHz %2 (%3) için Golden kayıt bulunamadı -> Canlı Referans Anten ölçümüne geçildi.")
                                .arg(ch.frequencyMhz).arg(ch.polarization).arg(ch.channelName));
        }
#if defined(NETA_ALGORITMA_2_THREE_PC)
        // Algorithm 2: Update MariaDB lock heartbeat and tune shared Reference DRX9000 via DataStream HAL
        DbManager::instance().updateDbLockHeartbeat(AppConfig::instance().stationId(), ch.channelNo);
        if (m_refStream) {
            m_refStream->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
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

        int sampleSec = AppConfig::instance().sampleDurationSec();
        if (m_currentChannelIsGolden && m_selectedGoldenRecord.id > 0 && m_selectedGoldenRecord.durationSec > 0 &&
            m_selectedGoldenRecord.frequencyMhz == m_profile.channels[m_currentChannelIndex].frequencyMhz) {
            sampleSec = m_selectedGoldenRecord.durationSec;
        }
        m_sampleDurationTimer.start(sampleSec * 1000);
        m_sampleTickTimer.start(250); // Sample telemetry at 250ms intervals
        emit logMessage(QString("Taşıyıcı kilitlendi (S:+). Telemetri toplanıyor: %1 saniye (%2)...")
                            .arg(sampleSec)
                            .arg(m_currentChannelIsGolden
                                     ? "Kayıtlı/Sabit Golden Referans"
                                     : "Canlı Referans Anten Testi"));
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
        // Ignore tuning transients until both DUT and Reference (or Golden) have locked valid signals
        if (!m_dutCarrierLocked || cnDb <= 0.05) {
            return;
        }
        if (!m_currentChannelIsGolden && (!m_refCarrierLocked || m_lastRefCn <= 0.05)) {
            return;
        }

        m_dutPowSamples.append(powerDbm);
        m_dutCnSamples.append(cnDb);

        double delta = cnDb - m_lastRefCn;
        const auto &ch = m_profile.channels[m_currentChannelIndex];
        emit channelSampleUpdated(ch.channelNo, cnDb, powerDbm, m_lastRefCn, m_lastRefPow, delta);
    }
}

void TestStateMachine::onDutTimestampReceived(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr) {
    m_lastDutDeviceTime = deviceTime;
    m_lastDutHostTime = hostTime;
    m_lastDutRawTimeStr = rawTimeStr;
    if (m_state == TestState::SamplingTelemetry || m_state == TestState::WaitingCarrierLock) {
        m_channelTimeHistory.append(qMakePair(hostTime, rawTimeStr));
    }
}

void TestStateMachine::onRefData(double refCn, double refPow, bool carrierLocked, bool videoLocked) {
    if (m_currentChannelIsGolden) {
        return; // Keep stored Golden Sample baseline unchanged
    }
    // Reject unlocked / channel-switching 0.0 dB tuning samples so charts and averages stay accurate
    if (!carrierLocked || refCn <= 0.05) {
        return;
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
    if (m_dutStream) {
        m_dutStream->requestStatus();
    }
#if defined(NETA_ALGORITMA_2_THREE_PC)
    if (!m_currentChannelIsGolden && m_refStream) {
        m_refStream->requestStatus();
    }
#endif
}

void TestStateMachine::onSampleFinished() {
    m_sampleTickTimer.stop();
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
    res.drxDeviceTime = m_lastDutRawTimeStr;
    res.sampleHostTime = m_lastDutHostTime.isValid() ? m_lastDutHostTime : QDateTime::currentDateTime();

    // Evaluate cumulative sample-to-sample variation and instantaneous stability on this channel's
    // samples so oscillating / sine-wave DUT signals cannot pass by arithmetic mean cancellation.
    double maxSampleDelta = qAbs(res.deltaDb);
    double meanAbsDelta = qAbs(res.deltaDb);
    double cumulativeStepDelta = 0.0;
    double dutSwing = 0.0;
    int outOfTolSamples = 0;

    if (!m_dutCnSamples.isEmpty()) {
        double minDut = m_dutCnSamples.first();
        double maxDut = m_dutCnSamples.first();
        double sumAbsDelta = 0.0;
        double prevSignedDelta = 0.0;
        maxSampleDelta = 0.0;

        for (int i = 0; i < m_dutCnSamples.size(); ++i) {
            const double dutVal = m_dutCnSamples[i];
            if (dutVal < minDut) minDut = dutVal;
            if (dutVal > maxDut) maxDut = dutVal;

            const double refVal = (i < m_refCnSamples.size()) ? m_refCnSamples[i] : res.refCn;
            const double signedDelta = dutVal - refVal;
            const double instAbsDelta = qAbs(signedDelta);
            sumAbsDelta += instAbsDelta;
            if (i > 0) {
                cumulativeStepDelta += qAbs(signedDelta - prevSignedDelta);
            }
            prevSignedDelta = signedDelta;

            if (instAbsDelta > maxSampleDelta) {
                maxSampleDelta = instAbsDelta;
            }
            if (instAbsDelta > res.toleranceDb) {
                ++outOfTolSamples;
            }
        }
        meanAbsDelta = sumAbsDelta / m_dutCnSamples.size();
        dutSwing = maxDut - minDut;
    }

    const bool unstableOscillation =
        (cumulativeStepDelta > res.toleranceDb) ||
        (meanAbsDelta > res.toleranceDb) ||
        (dutSwing > (res.toleranceDb * 2.0 + 0.5)) ||
        (m_dutCnSamples.size() >= 3 && maxSampleDelta > (res.toleranceDb * 1.25) && outOfTolSamples >= 2);

    // Tolerance Pass/Fail check
    if (!res.carrierLocked) {
        res.status = "FAIL";    // Carrier signal unlock failure
    } else if (qAbs(res.deltaDb) > res.toleranceDb || unstableOscillation) {
        res.status = "FAIL";    // Exceeded attenuation tolerance or unstable oscillation
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
        QString("Ch=%1 (%2 MHz %3) | DUT=%4 dB | REF=%5 dB | Delta=%6 dB | CumStepDelta=%7 dB | MaxInstDelta=%8 dB | Swing=%9 dB | Status=%10 | SistemZamanı=%11 | DRXSaati=%12")
            .arg(res.channelName)
            .arg(res.frequencyMhz)
            .arg(res.polarization)
            .arg(res.dutCn, 0, 'f', 2)
            .arg(res.refCn, 0, 'f', 2)
            .arg(res.deltaDb, 0, 'f', 2)
            .arg(cumulativeStepDelta, 0, 'f', 2)
            .arg(maxSampleDelta, 0, 'f', 2)
            .arg(dutSwing, 0, 'f', 2)
            .arg(res.status)
            .arg(res.sampleHostTime.toString("yyyy-MM-dd hh:mm:ss.zzz"))
            .arg(res.drxDeviceTime.isEmpty() ? "--" : res.drxDeviceTime));

    AppLogger::instance().logEvent("TIME_TELEMETRY",
        QString("HostTime: %1 | DrxTime: %2 | dt_ready: true (reserved_ms: %3) | Ch: %4")
            .arg(res.sampleHostTime.toString("yyyy-MM-dd hh:mm:ss.zzz"))
            .arg(res.drxDeviceTime.isEmpty() ? "N/A" : res.drxDeviceTime)
            .arg(res.dtReservedMs)
            .arg(res.channelName));

    emit logMessage(QString("[%1/%2] %3 Bitti: DUT=%4 dB, REF=%5 dB, Delta=%6 dB -> %7 [Sistem: %8 | DRX: %9]")
                        .arg(m_currentChannelIndex + 1)
                        .arg(m_profile.channels.size())
                        .arg(res.channelName)
                        .arg(res.dutCn, 0, 'f', 1)
                        .arg(res.refCn, 0, 'f', 1)
                        .arg(res.deltaDb, 0, 'f', 1)
                        .arg(res.status)
                        .arg(res.sampleHostTime.toString("hh:mm:ss.zzz"))
                        .arg(res.drxDeviceTime.isEmpty() ? "--" : res.drxDeviceTime));

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

    if (m_referenceMode != ReferenceMode::GoldenSample) {
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
    }

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

    m_testRun.lastDrxDeviceTime = m_lastDutRawTimeStr;
    m_testRun.testEndTime = QDateTime::currentDateTime();

    changeState(TestState::Completed);
    emit testFinished(m_testRun);

    AppLogger::instance().logProcess(
        QString("ST-%1").arg(m_testRun.stationId),
        "TEST_FINISH",
        QString("SN=%1 | Tester=%2 | Verdict=%3 | Duration=%4s | AvgDelta=%5 dB | MaxDelta=%6 dB | SistemBitiş=%7 | DRXSaati=%8")
            .arg(m_testRun.antennaSn, m_testRun.testerName, m_testRun.overallResult)
            .arg(m_testRun.durationSec)
            .arg(m_testRun.avgDeltaDb, 0, 'f', 2)
            .arg(m_testRun.maxDeltaDb, 0, 'f', 2)
            .arg(m_testRun.testEndTime.toString("yyyy-MM-dd hh:mm:ss.zzz"))
            .arg(m_testRun.lastDrxDeviceTime.isEmpty() ? "--" : m_testRun.lastDrxDeviceTime));

    AppLogger::instance().logEvent("TIME_TELEMETRY",
        QString("RunEnd: SN=%1 | HostEnd: %2 | DrxTime: %3 | dt_ready: true (reserved_ms: %4)")
            .arg(m_testRun.antennaSn)
            .arg(m_testRun.testEndTime.toString("yyyy-MM-dd hh:mm:ss.zzz"))
            .arg(m_testRun.lastDrxDeviceTime.isEmpty() ? "N/A" : m_testRun.lastDrxDeviceTime)
            .arg(m_testRun.dtReservedMs));

    emit logMessage(QString("=== TEST TAMAMLANDI: %1 (Süre: %2 sn, Ort. Delta: %3 dB | Sistem: %4 | DRX: %5) ===")
                        .arg(m_testRun.overallResult)
                        .arg(m_testRun.durationSec)
                        .arg(m_testRun.avgDeltaDb, 0, 'f', 2)
                        .arg(m_testRun.testEndTime.toString("hh:mm:ss.zzz"))
                        .arg(m_testRun.lastDrxDeviceTime.isEmpty() ? "--" : m_testRun.lastDrxDeviceTime));
}
