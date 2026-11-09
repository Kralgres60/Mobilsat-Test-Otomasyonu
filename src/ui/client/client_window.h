/**
 * @file client_window.h
 * @brief Operator test bench interface for factory workstations (Client Mode).
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
#include <QGridLayout>
#include "hardware/serial_worker.h"
#include "hardware/drx_web_client.h"
#include "network/tcp_client.h"
#include "network/udp_beacon.h"
#include "core/test_state_machine.h"
#include "channel_card_widget.h"
#include "ui/common/qcustomplot.h"

/**
 * @class ClientWindow
 * @brief Primary production window for station operators, providing barcode input,
 *        10-channel live telemetry cards, QCustomPlot RF charts, and test execution controls.
 */
class ClientWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ClientWindow(QWidget *parent = nullptr);
    ~ClientWindow() override;

private slots:
    void onStartTestClicked();
    void onAbortTestClicked();
    void onConnectSerialClicked();
    void onRefreshPortsClicked();

    // Network and Discovery Handlers
    void onHostDiscovered(const QString &hostIp, const QString &serverName, quint16 tcpPort);
    void onTcpConnected();
    void onTcpDisconnected();

    // Test Sequence State Handlers
    void onStateChanged(TestState state, const QString &stateName);
    void onChannelStarted(int channelNo, const QString &name, int freq, const QString &pol);
    void onChannelSampleUpdated(int channelNo, double dutCn, double dutPow, double refCn, double refPow, double delta);
    void onChannelCompleted(const ChannelSampleResult &result);
    void onTestFinished(const TestRunData &finalResult);
    void onWaitingForLockUpdate(int queuePos, int activeStation);
    void onToggleLogClicked();
    void onAboutClicked();

    void appendLog(const QString &text);

private:
    void setupUi();
    void setupPlot();
    void loadWorkOrdersAndProfiles();

    SerialWorker m_dutSerial;
    DrxWebClient m_dutWebClient;
    DrxWebClient m_refWebClient;
    TcpClient m_netClient;
    UdpBeaconListener m_beaconListener;
    TestStateMachine *m_stateMachine = nullptr;

    // UI Widgets
    QLabel *m_lblConnectionStatus = nullptr;
    QLabel *m_lblDeviceStatus = nullptr;
    QLabel *m_lblLockBanner = nullptr;
    QLabel *m_lblOverallResult = nullptr;
    QLabel *m_lblOverallDetails = nullptr;

    QComboBox *m_comboSerialPorts = nullptr;
    QPushButton *m_btnConnectSerial = nullptr;

    QComboBox *m_comboOrders = nullptr;
    QLineEdit *m_txtAntennaSn = nullptr;
    QPushButton *m_btnStartTest = nullptr;
    QPushButton *m_btnAbortTest = nullptr;

    QVector<ChannelCardWidget*> m_channelCards;
    QCustomPlot *m_plot = nullptr;
    QWidget *m_logDrawer = nullptr;
    QTextEdit *m_txtLog = nullptr;
    QPushButton *m_btnToggleLog = nullptr;

    QVector<WorkOrderInfo> m_workOrders;
    SatelliteProfileInfo m_currentProfile;
};

#endif // CLIENT_WINDOW_H
