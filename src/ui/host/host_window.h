/**
 * @file host_window.h
 * @brief Executive management server console and reference telemetry hub (Host Mode).
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
#include <QTabWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QMap>
#include "hardware/serial_worker.h"
#include "network/tcp_server.h"
#include "network/udp_beacon.h"
#include "core/lock_arbiter.h"
#include "core/xlsxutil.h"

/**
 * @class HostWindow
 * @brief Central controller dashboard providing multi-station lobby supervision,
 *        mutual exclusion tracking, historical quality test reporting, Excel export, and system configuration.
 */
class HostWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit HostWindow(QWidget *parent = nullptr);
    ~HostWindow() override;

private slots:
    void onConnectRefSerialClicked();
    void onRefreshPortsClicked();
    void onStationConnected(int stationId, const QString &ip);
    void onStationDisconnected(int stationId);
    void onStationStatusUpdated(const StationLiveStatus &status);

    void onLockGranted(int stationId, int timeoutSec);
    void onLockReleased(int stationId);
    void onLockExpired(int stationId);

    void onRefreshHistoryClicked();
    void onExportExcelClicked();
    void onSaveSettingsClicked();
    void onInstallMysqlClicked();
    void onResetDbClicked();
    void onCheckDbClicked();
    void onAboutClicked();

    void appendLog(const QString &msg);

private:
    void setupUi();
    void setupLobbyTab();
    void setupHistoryTab();
    void setupOrdersTab();
    void setupSettingsTab();

    LockArbiter m_arbiter;
    SerialWorker m_refSerial;
    TcpServer m_tcpServer;
    UdpBeaconBroadcaster m_beaconBroadcaster;

    // UI Widgets - Top Bar
    QLabel *m_lblServerStatus = nullptr;
    QLabel *m_lblRefTelemetry = nullptr;
    QComboBox *m_comboSerialPorts = nullptr;
    QPushButton *m_btnConnectRefSerial = nullptr;

    // UI Widgets - Lobby Tab
    QLabel *m_lblArbiterStatus = nullptr;

    struct StationCardWidgets {
        QLabel *lblState = nullptr;
        QLabel *lblIp = nullptr;
        QLabel *lblSn = nullptr;
        QLabel *lblChannel = nullptr;
    };
    QMap<int, StationCardWidgets> m_stationWidgets;

    // UI Widgets - History Tab
    QTableWidget *m_tableHistory = nullptr;
    QLineEdit *m_txtSearchSn = nullptr;
    QPushButton *m_btnRefreshHistory = nullptr;
    QPushButton *m_btnExportExcel = nullptr;

    // UI Widgets - Orders Tab
    QTableWidget *m_tableOrders = nullptr;

    // UI Widgets - Settings Tab
    QSpinBox *m_spinLockTimeout = nullptr;
    QSpinBox *m_spinSampleDuration = nullptr;
    QDoubleSpinBox *m_spinTolerance = nullptr;
    QLabel *m_lblDbStatus = nullptr;
    QLineEdit *m_txtDbHost = nullptr;
    QLineEdit *m_txtDbName = nullptr;

    // System Log
    QTextEdit *m_txtLog = nullptr;
};

#endif // HOST_WINDOW_H
