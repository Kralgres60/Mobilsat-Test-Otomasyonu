/**
 * @file client_window.h
 * @brief Clean sidebar-navigated operator and engineer test bench interface (Ethernet-first with ICMP ping check).
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
#include <QListWidget>
#include <QStackedWidget>
#include <QProgressBar>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTimer>
#include "network/tcp_client.h"
#include "network/udp_beacon.h"
#include "core/test_state_machine.h"
#include "ui/common/qcustomplot.h"
#include "ui/common/profile_manager_widget.h"
#include "ui/common/test_history_widget.h"
#include "hardware/data_transfer.h"

/**
 * @class ClientWindow
 * @brief Production workstation window using CAT6 Ethernet Web CGI + ICMP Ping verification.
 *
 * Layout: Left sidebar navigation (QListWidget#sideNav) + right QStackedWidget pages.
 *   - Technician: Production Test, Live Graph
 *   - Engineer:   same + Engineer Settings, System Logs
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
    /** @brief Applies the dynamic station ID assigned by the Host server on JOIN. */
    void onStationIdAssigned(int stationId);

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
    /** @brief Returns to the unified HOST / JOIN server lobby window. */
    void onReturnLobbyClicked();
    void onSaveEngineerSettingsClicked();
    void onSaveWebSettingsClicked();
    /** @brief Saves Station MariaDB settings and (re)connects to the central database. */
    void onReconnectDbClicked();
    void onClearGraphClicked();
    void onClearLogClicked();

    /** @brief Handles satellite profile combo box selection on the production page. */
    void onProfileComboChanged(int index);
    /** @brief Syncs default satellite profile when work order selection changes. */
    void onWorkOrderComboChanged(int index);
    /** @brief Updates station reference mode (Live vs Golden Sample) dictated by the Host. */
    void onReferenceModeChanged(ReferenceMode mode);

    /** @brief Switches the right-side page stack to the index selected in the sidebar. */
    void onNavItemChanged(int index);

    void appendLog(const QString &text);

private:
    void setupMenuBar();
    void setupUi();
    void setupProductionPage();
    void setupGraphPage();
    void setupProfilesPage();
    void setupEngineerSettingsPage();
    void setupLogsPage();
    void setupPlot();
    void loadWorkOrdersAndProfiles();
    void populateChannelTable();
    void resetLiveTelemetryPills();
    void applyUserRoleUi();
    /** @brief Updates the top production-page MariaDB status badge and Engineer Settings DB label. */
    void updateDbStatusBadge();
    void setStatusMessage(const QString &msg);

    RS232    m_dutSerial;
    Ethernet m_dutWebClient;
    Ethernet m_refWebClient;
    TcpClient m_netClient;
    UdpBeaconListener m_beaconListener;
    TestStateMachine *m_stateMachine = nullptr;
    QTimer m_ethPingTimer;

    // ── Sidebar Navigation ──────────────────────────────────────────────────
    /** @brief Left sidebar navigation list (objectName: "sideNav" for QSS targeting). */
    QListWidget    *m_sideNav   = nullptr;
    /** @brief Right-side page container matched to sidebar items. */
    QStackedWidget *m_pageStack = nullptr;

    // Page widgets (replace former QTabWidget tabs)
    QWidget *m_pageProduction = nullptr;  ///< Page 0 – Üretim Testi
    QWidget *m_pageGraph      = nullptr;  ///< Page 1 – Canlı Grafik
    QWidget *m_pageProfiles   = nullptr;  ///< Page 2 – Uydu Profilleri & Frekanslar
    TestHistoryWidget *m_historyWidget = nullptr; ///< Page 3 – Test Geçmişi & Rapor
    QWidget *m_pageEngineer   = nullptr;  ///< Page 4 – Mühendis Ayarları (Engineer only)
    QWidget *m_pageLogs       = nullptr;  ///< Page 5 – Sistem Logları (Engineer only)
    ProfileManagerWidget *m_profileManager = nullptr;

    // ── Station & Ethernet Status Pills (Page 0) ───────────────────────────
    QLabel *m_lblStationBadge    = nullptr;
    QLabel *m_lblConnectionStatus = nullptr;
    QLabel *m_lblDbBadge         = nullptr; ///< Live MariaDB connection badge in top status row
    QLabel *m_lblEthPingBadge    = nullptr;
    QLabel *m_lblLockBanner      = nullptr;
    QLabel *m_lblRefModeBadge    = nullptr;
    QLabel *m_lblRolePill        = nullptr;
    QLabel *m_lblOperatorName    = nullptr;
    QLabel *m_lblDeviceStatus    = nullptr;
    QLineEdit *m_txtHeaderDutIp    = nullptr; ///< Editable DUT DRX IP in main station header
    QLabel *m_lblDevMeta         = nullptr;
    QPushButton *m_btnEthPingSync = nullptr;

    // Work Order & Barcode Controls (Page 0)
    QComboBox  *m_comboOrders   = nullptr;
    QComboBox  *m_comboProfiles = nullptr;
    QLineEdit  *m_txtTesterName = nullptr;  ///< Mandatory per-test tester name (distinct from login username)
    QLineEdit  *m_txtAntennaSn  = nullptr;
    QPushButton *m_btnStartTest = nullptr;
    QPushButton *m_btnAbortTest = nullptr;

    // 3x3 Live RF Telemetry Pastel Pills (Page 0)
    QLabel *m_lblLiveChannel   = nullptr;
    QLabel *m_lblLiveFreqPol   = nullptr;
    QLabel *m_lblStatusSignal  = nullptr;
    QLabel *m_lblLiveDutCn     = nullptr;
    QLabel *m_lblLiveDelta     = nullptr;
    QLabel *m_lblStatusVideo   = nullptr;
    QLabel *m_lblLiveRefCn     = nullptr;
    QLabel *m_lblLivePower     = nullptr;
    QLabel *m_lblLiveTolerance = nullptr;

    // Outcome Summary & 10-Channel Table (Page 0 Right)
    QFrame      *m_resultBannerFrame  = nullptr;
    QLabel      *m_lblOverallResult   = nullptr;
    QLabel      *m_lblOverallDetails  = nullptr;
    QProgressBar *m_progressTest      = nullptr;
    QTableWidget *m_tableChannels     = nullptr;

    // Live Graph (Page 1)
    QCustomPlot *m_plot = nullptr;

    // Engineer Settings (Page 2)
    QDoubleSpinBox *m_spinTolerance      = nullptr;
    QSpinBox       *m_spinSampleDuration = nullptr;
    QLabel         *m_lblProfileSummary  = nullptr;
    QLineEdit      *m_txtDutDrxIp        = nullptr;
    QLineEdit      *m_txtRefDrxIp        = nullptr;
    QLineEdit      *m_txtDrxUser         = nullptr;
    QLineEdit      *m_txtDrxPass         = nullptr;
    QLineEdit      *m_txtHostIp          = nullptr;
    QSpinBox       *m_spinTcpPort        = nullptr;
    QSpinBox       *m_spinUdpPort        = nullptr;
    QLineEdit      *m_txtDbHost          = nullptr; ///< Central MariaDB server IP
    QSpinBox       *m_spinDbPort         = nullptr; ///< Central MariaDB server port
    QLineEdit      *m_txtDbName          = nullptr; ///< Central MariaDB database name
    QLineEdit      *m_txtDbUser          = nullptr; ///< Central MariaDB username
    QLineEdit      *m_txtDbPass          = nullptr; ///< Central MariaDB password
    QLabel         *m_lblDbConnStatus    = nullptr; ///< Live DB connection status badge
    QComboBox      *m_comboSerialPorts   = nullptr;
    QPushButton    *m_btnConnectSerial   = nullptr;

    // System Logs (Page 3)
    QTextEdit *m_txtLog = nullptr;

    // Status bar label
    QLabel *m_statusLabel = nullptr;

    QString m_activeAntennaSn;
    QVector<WorkOrderInfo> m_workOrders;
    QVector<SatelliteProfileInfo> m_profiles;
    SatelliteProfileInfo m_currentProfile;
};

#endif // CLIENT_WINDOW_H

