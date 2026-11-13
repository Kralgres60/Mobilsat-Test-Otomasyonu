/**
 * @file debug_runner_window.h
 * @brief Geliştirici ve hata ayıklama konsolu (DRX Web CGI & MariaDB Kilit ve Sorgu Testi).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DEBUG_RUNNER_WINDOW_H
#define DEBUG_RUNNER_WINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include <QTabWidget>
#include <QTableWidget>
#include <QProgressBar>
#include "hardware/data_transfer.h"
#include "database/db_manager.h"

/**
 * @class DbDebugWidget
 * @brief MariaDB bağlantı, şema doğrulama ve Algoritma 2 dağıtık kilit stres test bileşeni.
 */
class DbDebugWidget : public QWidget {
    Q_OBJECT
public:
    explicit DbDebugWidget(QWidget *parent = nullptr);
    ~DbDebugWidget() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onStartEngineClicked();
    void onStopEngineClicked();
    void onInitSchemaClicked();
    void onAcquireLockClicked();
    void onReleaseLockClicked();
    void onHeartbeatClicked();
    void onQueryLockClicked();
    void onRunContentionSimulationClicked();
    void onInsertMockRunClicked();
    void onRefreshTestRunsClicked();
    void onLoadProfilesClicked();
    void onRunFullSuiteClicked();
    void onSuiteTimerTick();

private:
    void setupUi();
    void appendLog(const QString &msg, const QString &color = "#E0E0E0");
    void updateConnectionStatusBadge(bool connected);

    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_dbNameEdit = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;
    QPushButton *m_startEngineBtn = nullptr;
    QPushButton *m_stopEngineBtn = nullptr;
    QLabel *m_statusBadge = nullptr;

    QPushButton *m_initSchemaBtn = nullptr;
    QPushButton *m_loadProfilesBtn = nullptr;

    QSpinBox *m_stationSpin = nullptr;
    QSpinBox *m_channelSpin = nullptr;
    QPushButton *m_acquireLockBtn = nullptr;
    QPushButton *m_releaseLockBtn = nullptr;
    QPushButton *m_heartbeatBtn = nullptr;
    QPushButton *m_queryLockBtn = nullptr;
    QPushButton *m_contentionSimBtn = nullptr;
    QLabel *m_lockHolderBadge = nullptr;

    QPushButton *m_insertMockBtn = nullptr;
    QPushButton *m_refreshRunsBtn = nullptr;
    QTableWidget *m_runsTable = nullptr;

    QTextEdit *m_logEdit = nullptr;

    QPushButton *m_runFullSuiteBtn = nullptr;
    QProgressBar *m_suiteProgress = nullptr;
    QLabel *m_stepDriverLabel = nullptr;
    QLabel *m_stepLatencyLabel = nullptr;
    QLabel *m_stepSchemaLabel = nullptr;
    QLabel *m_stepChannelsLabel = nullptr;
    QLabel *m_stepLockLabel = nullptr;
    QLabel *m_stepPersistenceLabel = nullptr;
    QLabel *m_suiteResultBanner = nullptr;

    QTimer m_pollTimer;
    QTimer m_suiteTimer;
    int m_suiteStep = 0;
};

/**
 * @class DebugRunnerWindow
 * @brief Standalone test GUI providing manual DRX Web CGI authentication, transponder tuning,
 *        5-second multi-sample averaging, and formatted raw JSON payload inspection.
 */
class DebugRunnerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DebugRunnerWindow(QWidget *parent = nullptr);
    ~DebugRunnerWindow() override;

private slots:
    void onConnectClicked();
    void onTuneClicked();
    void onPollTelemetryClicked();
    void onStartSamplingClicked();
    void onSampleTimerTick();
    void onSamplingFinished();
    void onSessionKeyReceived(const QString &sessionKey);
    void onTuneFinished(bool success, const QString &message);
    void onErrorOccurred(const QString &error);
    void onRawResponseReceived(const QString &actionType, const QByteArray &rawJson);
    void onScanAllClicked();
    void onCopyRawClicked();

private:
    void setupUi();
    void appendLog(const QString &msg, const QString &color = "#E0E0E0");

    QLineEdit *m_ipEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel *m_sessionBadge = nullptr;

    QSpinBox *m_freqSpin = nullptr;
    QSpinBox *m_srSpin = nullptr;
    QComboBox *m_polCombo = nullptr;
    QSpinBox *m_sampleDurationSpin = nullptr;
    QPushButton *m_tuneBtn = nullptr;
    QPushButton *m_pollBtn = nullptr;
    QPushButton *m_startSampleBtn = nullptr;
    QPushButton *m_scanAllBtn = nullptr;

    QLabel *m_carrierLabel = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_audioLabel = nullptr;
    QLabel *m_powerLabel = nullptr;
    QLabel *m_cnLabel = nullptr;
    QLabel *m_berLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_liveCnLabel = nullptr;
    QLabel *m_livePowLabel = nullptr;
    QLabel *m_liveEbN0Label = nullptr;
    QLabel *m_liveBerLabel = nullptr;
    QLabel *m_avgResultLabel = nullptr;

    QTabWidget *m_tabWidget = nullptr;
    QTextEdit *m_rawJsonEdit = nullptr;
    QTextEdit *m_logEdit = nullptr;
    QPushButton *m_copyRawBtn = nullptr;

    bool m_isSampling = false;
    QTimer m_sampleTickTimer;
    QTimer m_sampleDurationTimer;
    QVector<double> m_powSamples;
    QVector<double> m_cnSamples;

    Ethernet *m_webClient = nullptr;
    DbDebugWidget *m_dbDebugWidget = nullptr;
};

#endif // DEBUG_RUNNER_WINDOW_H
