/**
 * @file host_window.h
 * @brief Executive management server console and Ethernet reference telemetry hub (Host Mode).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef HOST_WINDOW_H
#define HOST_WINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QMap>
#include <QGridLayout>
#include "hardware/data_transfer.h"
#include "network/tcp_server.h"
#include "network/udp_beacon.h"
#include "core/lock_arbiter.h"
#include "core/xlsxutil.h"
#include "ui/common/test_history_widget.h"
#include "ui/host/golden_reference_widget.h"
#include "ui/host/user_manager_widget.h"

/**
 * @class HostWindow
 * @brief Central controller dashboard in NETA Yeşili theme providing multi-station lobby supervision,
 *        mutual exclusion tracking, Reference DRX Ethernet Ping & Web CGI control, reporting, and configuration.
 */
class HostWindow : public QMainWindow {
    Q_OBJECT
public:
    /** @brief Constructs the Host server dashboard and starts TCP server, UDP beacon, and Reference Ethernet Ping. */
    explicit HostWindow(QWidget *parent = nullptr);
    ~HostWindow() override;

private slots:
    void onRefEthPingSyncClicked();
    void onRefPingFinished(bool reachable, int latencyMs, const QString &hostIp);
    void onRefWebLoginSucceeded(int sessionKey);
    void onRefWebLoginFailed(const QString &reason);
    void onSaveRefWebSettingsClicked();

    // Optional Legacy RS232 Fallback (Settings tab only)
    void onConnectRefSerialClicked();
    void onRefreshPortsClicked();

    void onStationConnected(int stationId, const QString &ip);
    void onStationDisconnected(int stationId);
    void onStationStatusUpdated(const StationLiveStatus &status);

    void onLockGranted(int stationId, int timeoutSec);
    void onLockReleased(int stationId);
    void onLockExpired(int stationId);

    void onRefreshHistoryClicked();
    void onSaveSettingsClicked();
    void onInstallMysqlClicked();
    void onResetDbClicked();
    void onCheckDbClicked();
    void onAboutClicked();
    void onLogoutClicked();
    /** @brief Returns to the unified HOST / JOIN server lobby window. */
    void onReturnLobbyClicked();

    /** @brief Persists and broadcasts the Administrator's Reference Mode selection. */
    void onReferenceModeApplyRequested(ReferenceMode mode);

    /** @brief Switches the stacked page view when a sidebar navigation item is selected. */
    void onNavItemChanged(int index);

    void appendLog(const QString &msg);

protected:
    /** @brief Gracefully stops TCP server, UDP beacon, timers, and portable MariaDB on window close. */
    void closeEvent(QCloseEvent *event) override;

private:
    void shutdownHostServices();
    void setupMenuBar();
    void setupUi();
    void setupLobbyPage();
    void setupNetworkPcsPage();
    void setupGoldenPage();
    void setupHistoryPage();
    void setupOrdersPage();
    void setupSettingsPage();
    void setupLogsPage();
    void ensureStationCard(int stationId, const QString &stationName = QString());
    void refreshNetworkPcsTable();
    void updateRefModeBadgeUi(ReferenceMode mode);
    void setStatusMessage(const QString &msg);

    LockArbiter m_arbiter;
    RS232 m_refSerial;
    Ethernet m_refWebClient;
    TcpServer m_tcpServer;
    UdpBeaconBroadcaster m_beaconBroadcaster;
    QTimer m_refEthPingTimer;
    QTimer m_refTelemetryPollTimer; ///< Periodic idle telemetry poller for Reference/Golden DRX
    bool m_returningToLobby = false;
    bool m_servicesStopped = false;

    // ── Sidebar Navigation ──────────────────────────────────────────────────
    /** @brief Left sidebar navigation list (objectName: "sideNav" for QSS targeting). */
    QListWidget    *m_sideNav     = nullptr;
    /** @brief Right-side page container matched to sidebar items. */
    QStackedWidget *m_pageStack   = nullptr;
    QWidget        *m_pageLobby      = nullptr;  ///< Page 0 – Canlı Test Lobisi
    QWidget        *m_pageNetworkPcs = nullptr;  ///< Page 1 – Ağdaki (Serverdeki) PC'ler ve Durumları
    QWidget        *m_pageGolden     = nullptr;  ///< Page 2 – Referans & Golden Ölçüm Havuzu
    QWidget        *m_pageHistory    = nullptr;  ///< Page 3 – Test Geçmişi & Raporlama
    QWidget        *m_pageOrders     = nullptr;  ///< Page 4 – İş Emirleri
    QWidget        *m_pageSettings   = nullptr;  ///< Page 5 – Sistem Ayarları
    QWidget        *m_pageLogs       = nullptr;  ///< Page 6 – Sistem Logları
    UserManagerWidget *m_userManager = nullptr;  ///< Page 7 – Kullanıcı & Yetkilendirme Yönetimi

    // Top Header Card Widgets (NO serial port widgets on header)
    QLabel *m_lblServerStatus = nullptr;
    QLabel *m_lblRefModeBadge = nullptr;
    QLabel *m_lblRefPingBadge = nullptr;
    QLabel *m_lblRefTelemetry = nullptr;
    QLabel *m_lblOperatorName = nullptr;
    QLineEdit *m_txtHeaderRefIp = nullptr; ///< Editable Reference DRX IP in top header bar
    QPushButton *m_btnRefEthPingSync = nullptr;

    // Lobby Tab Widgets (dynamic N-station grid)
    QLabel      *m_lblArbiterStatus = nullptr;
    QLabel      *m_lblNoStations    = nullptr;
    QWidget     *m_stationsContainer = nullptr;
    QGridLayout *m_stationsGrid     = nullptr;

    struct StationCardWidgets {
        QFrame *cardFrame  = nullptr;
        QLabel *lblTitle   = nullptr;
        QLabel *lblState   = nullptr;
        QLabel *lblIp      = nullptr;
        QLabel *lblSn      = nullptr;
        QLabel *lblChannel = nullptr;
    };
    QMap<int, StationCardWidgets> m_stationWidgets;
    QMap<int, StationLiveStatus>  m_knownStations;

    // Network PCs Table Page Widgets
    QTableWidget *m_tableNetworkPcs   = nullptr;
    QLabel       *m_lblPcSummaryStats = nullptr;

    // History Tab Widgets
    TestHistoryWidget *m_historyWidget = nullptr;

    // Orders Tab Widgets
    QTableWidget *m_tableOrders = nullptr;

    // Settings Tab Widgets
    QSpinBox *m_spinLockTimeout = nullptr;
    QSpinBox *m_spinSampleDuration = nullptr;
    QDoubleSpinBox *m_spinTolerance = nullptr;
    QLineEdit *m_txtRefDrxIp = nullptr;
    QSpinBox *m_spinRefDrxPort = nullptr; ///< Reference DRX HTTP/CGI port in Settings
    QLineEdit *m_txtRefDrxUser = nullptr;
    QLineEdit *m_txtRefDrxPass = nullptr;
    QComboBox *m_comboSerialPorts = nullptr;
    QPushButton *m_btnConnectRefSerial = nullptr;
    QLabel *m_lblDbStatus = nullptr;
    QLineEdit *m_txtDbHost = nullptr;
    QSpinBox *m_spinDbPort = nullptr; ///< MariaDB port in Settings
    QLineEdit *m_txtDbName = nullptr;
    QSpinBox *m_spinHostTcpPort = nullptr; ///< Host TCP server port in Settings
    QSpinBox *m_spinHostUdpPort = nullptr; ///< Host UDP beacon port in Settings

    // Host Standalone Golden Reference Widget (NO satellite profiles on Host)
    GoldenReferenceWidget *m_goldenRefWidget = nullptr;

    // System Logs & Status Bar
    QTextEdit *m_txtLog = nullptr;
    QLabel *m_statusLabel = nullptr;
};

#endif // HOST_WINDOW_H
