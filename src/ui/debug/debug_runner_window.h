/**
 * @file debug_runner_window.h
 * @brief Interactive developer test console for DRX REST API, multi-sampling, and telemetry verification.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DEBUG_RUNNER_WINDOW_H
#define DEBUG_RUNNER_WINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include <QTabWidget>
#include "hardware/drx_web_client.h"

class DbDebugWidget;

/**
 * @class DebugRunnerWindow
 * @brief Standalone test GUI providing manual DRX Web CGI authentication, transponder tuning,
 *        5-second multi-sample averaging, and formatted raw JSON payload inspection.
 */
class DebugRunnerWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Constructs debug runner window.
     * @param parent Parent widget
     */
    explicit DebugRunnerWindow(QWidget *parent = nullptr);

    /** @brief Destructor. */
    ~DebugRunnerWindow() override;

private slots:
    /** @brief Authenticates with DRX receiver and requests session key. */
    void onConnectClicked();

    /** @brief Dispatches transponder tuning command (ResponseTo=freq). */
    void onTuneClicked();

    /** @brief Queries live RF alarmStatus telemetry once. */
    void onPollTelemetryClicked();

    /** @brief Starts 5-second multi-sampling loop at 250ms intervals. */
    void onStartSamplingClicked();

    /** @brief Periodic 250ms tick handler during sampling. */
    void onSampleTimerTick();

    /** @brief Computes averages and delta upon sampling completion. */
    void onSamplingFinished();

    /** @brief Invoked when session key is acquired. */
    void onSessionKeyReceived(const QString &sessionKey);

    /** @brief Invoked when RF tune operation completes. */
    void onTuneFinished(bool success, const QString &message);

    /** @brief Error handler. */
    void onErrorOccurred(const QString &error);

    /** @brief Invoked upon receiving raw HTTP JSON payload. */
    void onRawResponseReceived(const QString &actionType, const QByteArray &rawJson);

    /** @brief Scans all receiver pages sequentially (alarmStatus, channeldetails, frequency, deviceInfo). */
    void onScanAllClicked();

    /** @brief Copies pretty-printed JSON text to Windows clipboard. */
    void onCopyRawClicked();

private:
    void setupUi();
    void appendLog(const QString &msg, const QString &color = "#E0E0E0");

    // Hardware Client
    DrxWebClient *m_webClient = nullptr;

    // UI Widgets
    QLineEdit *m_ipEdit = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QSpinBox *m_freqSpin = nullptr;
    QSpinBox *m_srSpin = nullptr;
    QComboBox *m_polCombo = nullptr;
    QSpinBox *m_sampleDurationSpin = nullptr;

    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_tuneBtn = nullptr;
    QPushButton *m_pollBtn = nullptr;
    QPushButton *m_startSampleBtn = nullptr;
    QPushButton *m_scanAllBtn = nullptr;
    QPushButton *m_copyRawBtn = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_carrierLabel = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_audioLabel = nullptr;
    QLabel *m_liveCnLabel = nullptr;
    QLabel *m_livePowLabel = nullptr;
    QLabel *m_liveEbN0Label = nullptr;
    QLabel *m_liveBerLabel = nullptr;
    QLabel *m_avgResultLabel = nullptr;

    QTextEdit *m_logEdit = nullptr;
    QTextEdit *m_rawJsonEdit = nullptr;
    QTabWidget *m_tabWidget = nullptr;
    DbDebugWidget *m_dbDebugWidget = nullptr;

    // Sampling Variables
    QTimer m_sampleTickTimer;
    QTimer m_sampleDurationTimer;
    QVector<double> m_cnSamples;
    QVector<double> m_powSamples;
    bool m_isSampling = false;
};

#endif // DEBUG_RUNNER_WINDOW_H
