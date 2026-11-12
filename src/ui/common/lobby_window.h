/**
 * @file lobby_window.h
 * @brief Unified multiplayer-style Server Lobby (HOST or JOIN) shown to all authenticated users.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef LOBBY_WINDOW_H
#define LOBBY_WINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QMap>
#include "network/udp_beacon.h"

/**
 * @class LobbyWindow
 * @brief Common landing window after login where every workstation starts identically
 *        and chooses either HOST (create server as Manager PC) or JOIN (browse & join
 *        active LAN servers as a Test Station), transitioning into the corresponding sidebar workspace.
 */
class LobbyWindow : public QMainWindow {
    Q_OBJECT
public:
    /** @brief Constructs the unified HOST / JOIN lobby window and begins listening for LAN servers. */
    explicit LobbyWindow(QWidget *parent = nullptr);
    ~LobbyWindow() override;

private slots:
    void onHostServerClicked();
    void onJoinSelectedClicked();
    void onJoinManualClicked();
    void onServerRowDoubleClicked(int row, int column);
    void onSwitchUserClicked();

    void onHostDiscoveredInfo(const QString &hostIp, const QString &serverName, quint16 tcpPort, int stationCount, const QString &version);
    void onCleanupStaleServers();

private:
    void setupUi();
    void refreshServerTable();
    void launchHostMode();
    void launchClientMode(const QString &hostIp, quint16 tcpPort);

    struct DiscoveredServer {
        QString serverName;
        QString hostIp;
        quint16 tcpPort = 9100;
        int stationCount = 0;
        QString version;
        QDateTime lastSeen;
    };

    UdpBeaconListener m_beaconListener;
    QTimer m_cleanupTimer;
    QMap<QString, DiscoveredServer> m_servers; // Key: ip:port

    // Header
    QLabel *m_lblUserBadge = nullptr;
    QLabel *m_lblStatus    = nullptr;

    // HOST Card Widgets
    QLineEdit *m_txtServerName = nullptr;
    QSpinBox  *m_spinHostPort  = nullptr;

    // JOIN Card Widgets
    QLineEdit    *m_txtStationAlias = nullptr;
    QTableWidget *m_tableServers    = nullptr;
    QPushButton  *m_btnJoinSelected = nullptr;
    QLineEdit    *m_txtManualIp     = nullptr;
    QSpinBox     *m_spinManualPort  = nullptr;
};

#endif // LOBBY_WINDOW_H
