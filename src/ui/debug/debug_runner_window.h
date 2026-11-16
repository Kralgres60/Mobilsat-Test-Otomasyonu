/**
 * @file debug_runner_window.h
 * @brief Dedicated developer debug console for DRX Web CGI REST API queries and payload outputs.
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
#include "hardware/data_transfer.h"

/**
 * @class DebugRunnerWindow
 * @brief Streamlined developer GUI focused strictly on DRX REST API requests, live telemetry, and raw JSON outputs.
 */
class DebugRunnerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DebugRunnerWindow(QWidget *parent = nullptr);
    ~DebugRunnerWindow() override;

private slots:
    void onConnectClicked();
    void onPingClicked();
    void onTuneClicked();
    void onPollAlarmStatusClicked();
    void onPollDeviceInfoClicked();
    void onSendCustomPostClicked();
    void onStartSamplingClicked();
    void onSampleTimerTick();
    void onSamplingFinished();
    void onCopyRawClicked();
    void onClearOutputClicked();
    void onOpenDbToolClicked();

    // DRX Web Client Signal Handlers
    void onSessionKeyReceived(int sessionKey);
    void onLoginFailed(const QString &reason);
    void onTuneFinished(bool isOk, const QString &details);
    void onRawResponseReceived(const QString &actionName, const QByteArray &rawJson);
    void onTelemetryReceived(double powerDbm, double cnDb, double ber, double ebN0Db);
    void onStatusReceived(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName);
    void onTimestampReceived(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);

private:
    void setupUi();
    void appendLog(const QString &msg, const QString &color = "#E0E0E0");
    void updateSessionBadge(int sessionKey);
    void setPresetPayloadForAction(const QString &action);
    void syncEndpointSettings();

    // Connection Controls
    QLineEdit *m_txtIp = nullptr;
    QSpinBox *m_spinPort = nullptr;
    QLineEdit *m_txtUser = nullptr;
    QLineEdit *m_txtPass = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnPing = nullptr;
    QLabel *m_lblSessionBadge = nullptr;
    QPushButton *m_btnOpenDbTool = nullptr;

    // Frequency Tuning (Tune)
    QSpinBox *m_spinFreq = nullptr;
    QSpinBox *m_spinSymbolRate = nullptr;
    QComboBox *m_comboPol = nullptr;
    QPushButton *m_btnTune = nullptr;

    // Status and Sampling Controls
    QPushButton *m_btnAlarmStatus = nullptr;
    QPushButton *m_btnDeviceInfo = nullptr;
    QSpinBox *m_spinSampleSec = nullptr;
    QPushButton *m_btnStartSample = nullptr;

    // RF Telemetry Badges
    QLabel *m_lblCarrier = nullptr;
    QLabel *m_lblVideo = nullptr;
    QLabel *m_lblAudio = nullptr;
    QLabel *m_lblPower = nullptr;
    QLabel *m_lblCn = nullptr;
    QLabel *m_lblBer = nullptr;
    QLabel *m_lblEbN0 = nullptr;
    QLabel *m_lblChannelName = nullptr;
    QLabel *m_lblDeviceTime = nullptr;
    QLabel *m_lblDeltaTime = nullptr;

    // Custom CGI Request Controls
    QComboBox *m_comboCgiAction = nullptr;
    QTextEdit *m_txtCustomPayload = nullptr;
    QPushButton *m_btnSendCustom = nullptr;

    // Raw Output Panel (Core Focus)
    QLabel *m_lblResponseStatus = nullptr;
    QLabel *m_lblResponseLatency = nullptr;
    QTextEdit *m_txtRawOutput = nullptr;
    QPushButton *m_btnCopyRaw = nullptr;
    QPushButton *m_btnClearOutput = nullptr;

    // Bottom Log Console
    QTextEdit *m_txtLogs = nullptr;

    // Sampling State
    bool m_isSampling = false;
    QTimer m_sampleTickTimer;
    QTimer m_sampleDurationTimer;
    QVector<double> m_powSamples;
    QVector<double> m_cnSamples;

    Ethernet *m_webClient = nullptr;
};

#endif // DEBUG_RUNNER_WINDOW_H
