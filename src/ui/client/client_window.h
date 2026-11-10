/**
 * @file client_window.h
 * @brief Clean tabbed operator and engineer test bench interface (Ethernet-first with ICMP ping check).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef CLIENT_WINDOW_H
#define CLIENT_WINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QProgressBar>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTimer>
#include "hardware/serial_worker.h"
#include "hardware/drx_web_client.h"
#include "network/tcp_client.h"
#include "network/udp_beacon.h"
#include "core/test_state_machine.h"
#include "ui/common/qcustomplot.h"

/**
 * @class ClientWindow
 * @brief Production workstation window using CAT6 Ethernet Web CGI + ICMP Ping verification:
 *        - Technician: Clean Üretim Testi tab (Ethernet Ping->Data handshake, barcode scan,
 *          3x3 pastel telemetry, 10-channel table, PASS/FAIL verdict) & Live Graph.
 *        - Engineer: All Technician tabs plus RF/Ethernet/Optional-Serial settings tab & System Logs tab.
 */
class ClientWindow : public QMainWindow {
    Q_OBJECT
public:
    /** @brief Constructs the workstation main window and initializes Ethernet/network links. */
    explicit ClientWindow(QWidget *parent = nullptr);
    ~ClientWindow() override;

private slots:
    void onStartTestClicked();
    void onAbortTestClicked();
    void onEthPingSyncClicked();
    void onReconnectHostClicked();

    // Optional Legacy RS232 Fallback (Engineer tab only)
    void onConnectSerialClicked();
    void onRefreshPortsClicked();

    // Network and Discovery Handlers
    void onHostDiscovered(const QString &hostIp, const QString &serverName, quint16 tcpPort);
    void onTcpConnected();
    void onTcpDisconnected();

    // DRX Ethernet Ping & Web CGI Handlers
    void onEthPingFinished(bool reachable, int latencyMs, const QString &hostIp);
    void onWebLoginSucceeded(int sessionKey);
    void onWebLoginFailed(const QString &reason);
    void onWebDeviceInfoReceived(const QString &swVersion, const QString &deviceId, const QString &mac, const QString &tunerModel);

    // Test Sequence State Handlers
    void onStateChanged(TestState state, const QString &stateName);
    void onChannelStarted(int channelNo, const QString &name, int freq, const QString &pol);
    void onChannelSampleUpdated(int channelNo, double dutCn, double dutPow, double refCn, double refPow, double delta);
    void onChannelCompleted(const ChannelSampleResult &result);
    void onTestFinished(const TestRunData &finalResult);
    void onWaitingForLockUpdate(int queuePos, int activeStation);

    // Actions & Engineer Configuration
    void onAboutClicked();
    void onLogoutClicked();
    void onSaveEngineerSettingsClicked();
    void onSaveWebSettingsClicked();
    void onClearGraphClicked();
    void onClearLogClicked();

    void appendLog(const QString &text);

private:
    void setupMenuBar();
    void setupUi();
    void setupProductionTab();
    void setupGraphTab();
    void setupEngineerSettingsTab();
    void setupLogsTab();
    void setupPlot();
    void loadWorkOrdersAndProfiles();
    void populateChannelTable();
    void resetLiveTelemetryPills();
    void applyUserRoleUi();
    void setStatusMessage(const QString &msg);

    SerialWorker m_dutSerial;
    DrxWebClient m_dutWebClient;
    DrxWebClient m_refWebClient;
    TcpClient m_netClient;
    UdpBeaconListener m_beaconListener;
    TestStateMachine *m_stateMachine = nullptr;
    QTimer m_ethPingTimer;

    // Navigation & Tabs
    QTabWidget *m_tabWidget = nullptr;
    QWidget *m_tabProduction = nullptr;
    QWidget *m_tabGraph = nullptr;
    QWidget *m_tabEngineer = nullptr;
    QWidget *m_tabLogs = nullptr;

    // Station & Ethernet Status Pills (Tab 0)
    QLabel *m_lblStationBadge = nullptr;
    QLabel *m_lblConnectionStatus = nullptr;
    QLabel *m_lblEthPingBadge = nullptr;
    QLabel *m_lblLockBanner = nullptr;
    QLabel *m_lblRolePill = nullptr;
    QLabel *m_lblOperatorName = nullptr;
    QLabel *m_lblDeviceStatus = nullptr;
    QLabel *m_lblEthTargetIp = nullptr;
    QLabel *m_lblDevMeta = nullptr;
    QPushButton *m_btnEthPingSync = nullptr;

    // Work Order & Barcode Controls (Tab 0)
    QComboBox *m_comboOrders = nullptr;
    QLineEdit *m_txtAntennaSn = nullptr;
    QPushButton *m_btnStartTest = nullptr;
    QPushButton *m_btnAbortTest = nullptr;

    // 3x3 Live RF Telemetry Pastel Pills (Tab 0)
    QLabel *m_lblLiveChannel = nullptr;
    QLabel *m_lblLiveFreqPol = nullptr;
    QLabel *m_lblStatusSignal = nullptr;
    QLabel *m_lblLiveDutCn = nullptr;
    QLabel *m_lblLiveDelta = nullptr;
    QLabel *m_lblStatusVideo = nullptr;
    QLabel *m_lblLiveRefCn = nullptr;
    QLabel *m_lblLivePower = nullptr;
    QLabel *m_lblLiveTolerance = nullptr;

    // Outcome Summary & 10-Channel Table (Tab 0 Right)
    QFrame *m_resultBannerFrame = nullptr;
    QLabel *m_lblOverallResult = nullptr;
    QLabel *m_lblOverallDetails = nullptr;
    QProgressBar *m_progressTest = nullptr;
    QTableWidget *m_tableChannels = nullptr;

    // Live Graph (Tab 1)
    QCustomPlot *m_plot = nullptr;

    // Engineer Settings (Tab 2)
    QDoubleSpinBox *m_spinTolerance = nullptr;
    QSpinBox *m_spinSampleDuration = nullptr;
    QLabel *m_lblProfileSummary = nullptr;
    QLineEdit *m_txtDutDrxIp = nullptr;
    QLineEdit *m_txtRefDrxIp = nullptr;
    QLineEdit *m_txtDrxUser = nullptr;
    QLineEdit *m_txtDrxPass = nullptr;
    QLineEdit *m_txtHostIp = nullptr;
    QSpinBox *m_spinTcpPort = nullptr;
    QSpinBox *m_spinUdpPort = nullptr;
    QComboBox *m_comboSerialPorts = nullptr;
    QPushButton *m_btnConnectSerial = nullptr;

    // System Logs (Tab 3)
    QTextEdit *m_txtLog = nullptr;

    // Status bar label
    QLabel *m_statusLabel = nullptr;

    QString m_activeAntennaSn;
    QVector<WorkOrderInfo> m_workOrders;
    SatelliteProfileInfo m_currentProfile;
};

#endif // CLIENT_WINDOW_H
