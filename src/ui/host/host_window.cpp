/**
 * @file host_window.cpp
 * @brief Implementation of the NETA Yeşili Central Host dashboard with sidebar navigation and Reference Ethernet Ping & Web CGI control.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "host_window.h"
#include "appinfo.h"
#include "app_config.h"
#include "build_config.h"
#include "core/app_logger.h"
#include "database/db_manager.h"
#include "core/dependency_installer.h"
#include "ui/common/style_helper.h"
#include "ui/common/about_dialog.h"
#include "ui/common/login_dialog.h"
#include "ui/common/lobby_window.h"
#include "ui/client/client_window.h"
#include "ui/debug/debug_runner_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QScrollArea>
#include <QHeaderView>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QPixmap>
#include <QSplitter>
#include <QFrame>

HostWindow::HostWindow(QWidget *parent)
    : QMainWindow(parent),
      m_tcpServer(&m_arbiter, &m_refSerial, this),
      m_beaconBroadcaster(AppConfig::instance().udpBeaconPort(), AppConfig::instance().tcpPort(), this) {

    setAttribute(Qt::WA_DeleteOnClose);

#if defined(NETA_ALGORITMA_1_FOUR_PC)
    QString algoTitle = "[4 PC - Yönetici Sunucu]";
#elif defined(NETA_ALGORITMA_2_THREE_PC)
    QString algoTitle = "[3 PC - İzleme Paneli]";
#else
    QString algoTitle = "";
#endif

    setWindowTitle(QString("%1 - Fabrika Yönetim Sunucusu (HOST) %2 v%3")
                       .arg(NETA_APP_TITLE)
                       .arg(algoTitle)
                       .arg(NETA_APP_VERSION));
    setMinimumSize(1180, 740);
    setWindowState(Qt::WindowMaximized);

    // Primary Reference CAT6 Ethernet Web CGI Client
    m_refWebClient.setHost(AppConfig::instance().refDrxHost(), AppConfig::instance().refDrxPort());
    m_refWebClient.setCredentials(AppConfig::instance().drxUsername(), AppConfig::instance().drxPassword());
    m_tcpServer.setRefWebClient(&m_refWebClient);

    setupMenuBar();
    setupUi();

    // Reference Ethernet Ping & Web CGI Signal Wiring
    connect(&m_refWebClient, &DrxWebClient::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog((isErr ? "[REF ETH HATA] " : "[REF ETH] ") + msg);
    });
    connect(&m_refWebClient, &DrxWebClient::pingFinished, this, &HostWindow::onRefPingFinished);
    connect(&m_refWebClient, &DrxWebClient::loginSucceeded, this, &HostWindow::onRefWebLoginSucceeded);
    connect(&m_refWebClient, &DrxWebClient::loginFailed, this, &HostWindow::onRefWebLoginFailed);
    connect(&m_refWebClient, &DrxWebClient::telemetryReceived, this, [this](double powDbm, double cnDb, double /*ber*/, double /*ebn0*/) {
        m_lblRefTelemetry->setText(QString("📡 Ref Canlı: C/N=%1 dB | POW=%2 dBm").arg(cnDb, 0, 'f', 1).arg(powDbm, 0, 'f', 1));
        m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelBlueStyle());
    });

    // Network & Arbiter Signal Wiring
    connect(&m_tcpServer, &TcpServer::stationConnected, this, &HostWindow::onStationConnected);
    connect(&m_tcpServer, &TcpServer::stationDisconnected, this, &HostWindow::onStationDisconnected);
    connect(&m_tcpServer, &TcpServer::stationStatusUpdated, this, &HostWindow::onStationStatusUpdated);
    connect(&m_tcpServer, &TcpServer::logMessage, this, &HostWindow::appendLog);

    connect(&m_arbiter, &LockArbiter::lockGranted, this, &HostWindow::onLockGranted);
    connect(&m_arbiter, &LockArbiter::lockReleased, this, &HostWindow::onLockReleased);
    connect(&m_arbiter, &LockArbiter::lockExpired, this, &HostWindow::onLockExpired);

    // Optional Legacy Reference RS232 Fallback Wiring (Settings tab only)
    connect(&m_refSerial, &SerialWorker::portOpened, this, [this](const QString &p) {
        if (m_btnConnectRefSerial) {
            m_btnConnectRefSerial->setText("RS232 Kes");
            m_btnConnectRefSerial->setStyleSheet("background-color: #B91C1C; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
        }
        appendLog("[Ref RS232 Yedek] Seri port açıldı: " + p);
    });
    connect(&m_refSerial, &SerialWorker::portClosed, this, [this]() {
        if (m_btnConnectRefSerial) {
            m_btnConnectRefSerial->setText("RS232 Bağlan");
            m_btnConnectRefSerial->setStyleSheet("background-color: #0284C7; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
        }
        appendLog("[Ref RS232 Yedek] Seri port kapatıldı.");
    });
    connect(&m_refSerial, &SerialWorker::telemetryReceived, this, [this](double powDbm, double cnDb, double /*ber*/) {
        m_lblRefTelemetry->setText(QString("📡 Ref Canlı: C/N=%1 dB | POW=%2 dBm").arg(cnDb, 0, 'f', 1).arg(powDbm, 0, 'f', 1));
        m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelBlueStyle());
    });

    // Background 8s Ping Heartbeat for Reference DRX9000
    connect(&m_refEthPingTimer, &QTimer::timeout, this, [this]() {
        m_refWebClient.pingHost(1000);
    });
    m_refEthPingTimer.start(8000);

    // Periodic 2s idle telemetry polling so Host UI & Simulator stay live with C/N and POW
    connect(&m_refTelemetryPollTimer, &QTimer::timeout, this, [this]() {
        if (m_goldenRefWidget && m_goldenRefWidget->isCaptureActive()) {
            return;
        }
        if (m_refWebClient.isReachable() && m_refWebClient.isLoggedIn()) {
            m_refWebClient.requestStatus();
        } else if (m_refSerial.isConnected()) {
            m_refSerial.requestStatus();
        }
    });
    m_refTelemetryPollTimer.start(2000);

    // Start TCP Server & UDP Beacon
    m_tcpServer.start(AppConfig::instance().tcpPort());
    m_beaconBroadcaster.setServerName(AppConfig::instance().serverName());
    m_beaconBroadcaster.setStationCount(0);
    m_beaconBroadcaster.start(2000);
    appendLog(QString("Lobi yayını başladı (UDP %1, TCP %2).").arg(AppConfig::instance().udpBeaconPort()).arg(AppConfig::instance().tcpPort()));
    setStatusMessage(QString("Yönetici Sunucu Aktif (TCP %1 / UDP %2) | Referans Ethernet Ping kontrol ediliyor...")
                         .arg(AppConfig::instance().tcpPort()).arg(AppConfig::instance().udpBeaconPort()));

    onRefreshPortsClicked();
    if (DbManager::instance().isConnected()) {
        DbManager::instance().ensureDefaultDataIfMissing();
    }
    ReferenceMode activeMode = DbManager::instance().getReferenceMode();
    m_tcpServer.setReferenceMode(activeMode);
    updateRefModeBadgeUi(activeMode);
    if (m_goldenRefWidget) {
        m_goldenRefWidget->setActiveReferenceMode(activeMode);
        m_goldenRefWidget->reloadFrequencies();
    }
    onRefreshHistoryClicked();

    // Immediate startup sequence: ICMP Ping -> Web CGI Login -> DeviceInfo & Status Pull
    QTimer::singleShot(250, this, &HostWindow::onRefEthPingSyncClicked);
}

HostWindow::~HostWindow() {
    shutdownHostServices();
}

void HostWindow::shutdownHostServices() {
    if (m_servicesStopped) return;
    m_servicesStopped = true;

    AppConfig::instance().save();
    m_refEthPingTimer.stop();
    m_refTelemetryPollTimer.stop();
    m_beaconBroadcaster.stop();
    m_tcpServer.stop();
    m_refSerial.closePort();
    DbManager::instance().stopPortableMariaDbIfRunning();
}

void HostWindow::closeEvent(QCloseEvent *event) {
    shutdownHostServices();
    event->accept();
    if (!m_returningToLobby) {
        QCoreApplication::quit();
    }
}

void HostWindow::setupMenuBar() {
    QMenu *menuOturum = menuBar()->addMenu("Oturum");
    QAction *actLobby = menuOturum->addAction("🏠 Sunucu Lobisine Dön (HOST / JOIN)");
    connect(actLobby, &QAction::triggered, this, &HostWindow::onReturnLobbyClicked);

    QAction *actLogout = menuOturum->addAction("🔄 Kullanıcı Değiştir");
    actLogout->setShortcut(QKeySequence("Ctrl+L"));
    connect(actLogout, &QAction::triggered, this, &HostWindow::onLogoutClicked);

    menuOturum->addSeparator();
    QAction *actQuit = menuOturum->addAction("Programı Kapat");
    actQuit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    QMenu *menuBaglanti = menuBar()->addMenu("Bağlantı");
    QAction *actEthSync = menuBaglanti->addAction("Referans Ethernet Ping & Veri Yenile");
    actEthSync->setShortcut(QKeySequence("Ctrl+P"));
    connect(actEthSync, &QAction::triggered, this, &HostWindow::onRefEthPingSyncClicked);

    QMenu *menuAraclar = menuBar()->addMenu("Araçlar");
    QAction *actDebug = menuAraclar->addAction("🛠️ Geliştirici & Hata Ayıklama Konsolu");
    actDebug->setShortcut(QKeySequence("F12"));
    connect(actDebug, &QAction::triggered, this, [this]() {
        auto *debugWin = new DebugRunnerWindow(this);
        debugWin->setAttribute(Qt::WA_DeleteOnClose);
        debugWin->show();
    });

    QMenu *menuAyarlar = menuBar()->addMenu("Ayarlar");
    QAction *actAbout = menuAyarlar->addAction("Hakkında");
    actAbout->setShortcut(QKeySequence("Alt+A"));
    connect(actAbout, &QAction::triggered, this, &HostWindow::onAboutClicked);
}

void HostWindow::setupUi() {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 6, 8, 6);

    // 1. Top Executive Summary Card (CAT6 Ethernet + Ping status, NO serial ports on main header)
    QGroupBox *grpHeader = new QGroupBox("🏢 Fabrika Yönetim Sunucusu & Referans Çanak (Ethernet)", central);
    QHBoxLayout *topLayout = new QHBoxLayout(grpHeader);
    topLayout->setSpacing(8);

    QLabel *lblLogo = new QLabel(grpHeader);
    lblLogo->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 3px 10px;");
    QPixmap pix(":/resources/images/logo.png");
    if (!pix.isNull()) {
        lblLogo->setPixmap(pix.scaled(118, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        lblLogo->setText("<b style='color:#064E3B; font-size:13pt;'>NETA</b>");
    }

    m_lblServerStatus = new QLabel(QString("🟢 Host Aktif (Port %1)").arg(AppConfig::instance().tcpPort()), grpHeader);
    m_lblServerStatus->setStyleSheet(StyleHelper::getPastelGreenStyle());

    m_lblRefModeBadge = new QLabel("🟢 Mod: Canlı Referans", grpHeader);
    m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());

    m_lblRefPingBadge = new QLabel("📶 Ref Ping: Test...", grpHeader);
    m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());

    m_lblRefTelemetry = new QLabel("🔴 Ref Web: Bekleniyor", grpHeader);
    m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelRedStyle());

    QLabel *lblRoleBadge = new QLabel("HOST YÖNETİCİ", grpHeader);
    lblRoleBadge->setStyleSheet("background-color: #059669; color: white; border-radius: 4px; padding: 3px 8px; font-weight: bold;");

    m_lblOperatorName = new QLabel(AppConfig::instance().currentUserFullName(), grpHeader);
    m_lblOperatorName->setStyleSheet("color: #34D399; font-weight: bold;");

    QLabel *lblRefIpTitle = new QLabel("🌐 Ref DRX IP:", grpHeader);
    lblRefIpTitle->setStyleSheet("color: #34D399; font-weight: bold;");

    m_txtHeaderRefIp = new QLineEdit(AppConfig::instance().refDrxHost(), grpHeader);
    m_txtHeaderRefIp->setPlaceholderText("192.168.0.167 veya 127.0.0.1:8080");
    m_txtHeaderRefIp->setFixedWidth(185);
    m_txtHeaderRefIp->setToolTip("Referans DRX9000 IP adresi (Simülatör için örn: 127.0.0.1:8080)");
    m_txtHeaderRefIp->setStyleSheet("background-color: #0E2219; color: #34D399; border: 1px solid #1A3D2B; border-radius: 4px; padding: 4px 8px; font-weight: bold;");
    auto applyHeaderRefIp = [this]() {
        if (!m_txtHeaderRefIp) return;
        const QString enteredIp = m_txtHeaderRefIp->text().trimmed();
        if (enteredIp.isEmpty()) return;
        AppConfig &cfg = AppConfig::instance();
        if (enteredIp != cfg.refDrxHost()) {
            cfg.setRefDrxHost(enteredIp);
            cfg.save();
            m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
            if (m_txtRefDrxIp) {
                m_txtRefDrxIp->blockSignals(true);
                m_txtRefDrxIp->setText(enteredIp);
                m_txtRefDrxIp->blockSignals(false);
            }
            setStatusMessage(QString("Referans DRX IP güncellendi: %1").arg(enteredIp));
        }
    };
    connect(m_txtHeaderRefIp, &QLineEdit::editingFinished, this, applyHeaderRefIp);
    connect(m_txtHeaderRefIp, &QLineEdit::returnPressed, this, &HostWindow::onRefEthPingSyncClicked);

    m_btnRefEthPingSync = new QPushButton("🔌 Ref Ping & Bağlan (ETH)", grpHeader);
    m_btnRefEthPingSync->setCursor(Qt::PointingHandCursor);
    m_btnRefEthPingSync->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnRefEthPingSync, &QPushButton::clicked, this, &HostWindow::onRefEthPingSyncClicked);

    auto *btnLaunchSim = new QPushButton("🧪 Simülatörü Aç", grpHeader);
    btnLaunchSim->setCursor(Qt::PointingHandCursor);
    btnLaunchSim->setToolTip("NETA DRX9000 TCP Simülatörünü başlat");
    btnLaunchSim->setStyleSheet("background-color: #1D4ED8; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(btnLaunchSim, &QPushButton::clicked, this, [this]() { AppConfig::launchSimulator(this); });

    topLayout->addWidget(lblLogo);
    topLayout->addWidget(m_lblServerStatus);
    topLayout->addWidget(m_lblRefModeBadge);
    topLayout->addWidget(m_lblRefPingBadge);
    topLayout->addWidget(m_lblRefTelemetry);
    topLayout->addWidget(lblRoleBadge);
    topLayout->addWidget(m_lblOperatorName);
    topLayout->addStretch();
    topLayout->addWidget(lblRefIpTitle);
    topLayout->addWidget(m_txtHeaderRefIp);
    topLayout->addWidget(m_btnRefEthPingSync);
    topLayout->addWidget(btnLaunchSim);
    mainLayout->addWidget(grpHeader);

    // 2. Sidebar + Stacked Workspace
    QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setHandleWidth(2);
    splitter->setChildrenCollapsible(false);

    m_sideNav = new QListWidget(splitter);
    m_sideNav->setObjectName("sideNav");
    m_sideNav->setFixedWidth(215);
    m_sideNav->setSpacing(2);
    m_sideNav->setFrameShape(QFrame::NoFrame);

    QListWidgetItem *headerItem = new QListWidgetItem("  🏢  HOST YÖNETİCİ");
    headerItem->setFlags(Qt::NoItemFlags);
    headerItem->setFont(QFont("Segoe UI", 10, QFont::Bold));
    headerItem->setForeground(QColor("#34D399"));
    headerItem->setBackground(QColor("#064E3B"));
    headerItem->setSizeHint(QSize(215, 42));
    m_sideNav->addItem(headerItem);

    QListWidgetItem *sepItem = new QListWidgetItem();
    sepItem->setFlags(Qt::NoItemFlags);
    sepItem->setSizeHint(QSize(215, 1));
    sepItem->setBackground(QColor("#1A3D2B"));
    m_sideNav->addItem(sepItem);

    auto addNavItem = [&](const QString &text) {
        QListWidgetItem *item = new QListWidgetItem(text);
        item->setSizeHint(QSize(215, 46));
        m_sideNav->addItem(item);
    };
    addNavItem("  🎮  Canlı Test Lobisi");
    addNavItem("  🖥️  Ağdaki PC'ler");
    addNavItem("  🌟  Referans & Golden Ölçüm");
    addNavItem("  📊  Test Geçmişi & Veritabanı");
    addNavItem("  📦  İş Emirleri");
    addNavItem("  ⚙️  Sistem Ayarları");
    addNavItem("  📜  Sistem Logları");
    addNavItem("  🔐  Kullanıcı & Yetki");

    m_sideNav->setCurrentRow(2);

    m_pageStack = new QStackedWidget(splitter);
    setupLobbyPage();
    setupNetworkPcsPage();
    setupGoldenPage();
    setupHistoryPage();
    setupOrdersPage();
    setupSettingsPage();
    setupLogsPage();
    m_userManager = new UserManagerWidget(this);
    connect(m_userManager, &UserManagerWidget::logMessage, this, &HostWindow::appendLog);
    connect(m_userManager, &UserManagerWidget::usersUpdated, &m_tcpServer, &TcpServer::broadcastUserRoleUpdates);

    m_pageStack->addWidget(m_pageLobby);      // index 0
    m_pageStack->addWidget(m_pageNetworkPcs); // index 1
    m_pageStack->addWidget(m_pageGolden);     // index 2
    m_pageStack->addWidget(m_pageHistory);    // index 3
    m_pageStack->addWidget(m_pageOrders);     // index 4
    m_pageStack->addWidget(m_pageSettings);   // index 5
    m_pageStack->addWidget(m_pageLogs);       // index 6
    m_pageStack->addWidget(m_userManager);    // index 7

    splitter->addWidget(m_sideNav);
    splitter->addWidget(m_pageStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter, 1);

    connect(m_sideNav, &QListWidget::currentRowChanged, this, &HostWindow::onNavItemChanged);

    // 3. Clean Single-Line Status Bar
    m_statusLabel = new QLabel("Hazır", this);
    m_statusLabel->setStyleSheet("color: #A7C4B5; padding-left: 4px;");
    statusBar()->addWidget(m_statusLabel, 1);

    QLabel *lblFooterRight = new QLabel(
        QString("NETA Elektronik A.Ş.  |  Geliştirici: %1  |  v%2")
            .arg(NETA_APP_DEVELOPER, NETA_APP_VERSION), this);
    lblFooterRight->setStyleSheet("color: #4B7A66; padding-right: 6px; font-size: 8.5pt;");
    statusBar()->addPermanentWidget(lblFooterRight);
}

/**
 * @brief Switches the stacked page view when a sidebar navigation row is selected.
 * @param row Selected row index inside m_sideNav (rows 0..1 are header & separator).
 */
void HostWindow::onNavItemChanged(int row) {
    if (!m_pageStack) return;
    int pageIdx = row - 2;
    if (pageIdx >= 0 && pageIdx < m_pageStack->count()) {
        m_pageStack->setCurrentIndex(pageIdx);
        if (m_pageStack->widget(pageIdx) == m_userManager && m_userManager) {
            m_userManager->reloadUsers();
        } else if (m_pageStack->widget(pageIdx) == m_pageHistory && m_historyWidget) {
            m_historyWidget->refreshHistory();
        }
    }
}

void HostWindow::setupLobbyPage() {
    m_pageLobby = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageLobby);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(10);

    QGroupBox *grpArbiter = new QGroupBox("🔒 Merkezi Referans Kilit Durumu (Mutex Arbiter)", m_pageLobby);
    QHBoxLayout *arbLayout = new QHBoxLayout(grpArbiter);
    m_lblArbiterStatus = new QLabel("🟢 Referans Alıcısı BOŞTA (Tüm İstasyonlar İçin Müsait)", grpArbiter);
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");
    arbLayout->addWidget(m_lblArbiterStatus);
    vbox->addWidget(grpArbiter);

    QGroupBox *grpStations = new QGroupBox("🖥️ Bağlı Test İstasyonları (Sınırsız Dinamik Lobi)", m_pageLobby);
    QVBoxLayout *stBox = new QVBoxLayout(grpStations);

    m_lblNoStations = new QLabel(
        "⏳ Henüz bağlı test istasyonu yok. Ağdaki istasyonlar Lobiden JOIN yaptığında kartları burada otomatik oluşacaktır.",
        grpStations);
    m_lblNoStations->setAlignment(Qt::AlignCenter);
    m_lblNoStations->setStyleSheet("color: #A7C4B5; padding: 24px; font-size: 10.5pt; font-style: italic;");
    stBox->addWidget(m_lblNoStations);

    QScrollArea *scroll = new QScrollArea(grpStations);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_stationsContainer = new QWidget(scroll);
    m_stationsGrid = new QGridLayout(m_stationsContainer);
    m_stationsGrid->setSpacing(12);
    m_stationsGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(m_stationsContainer);
    stBox->addWidget(scroll, 1);

    vbox->addWidget(grpStations, 1);
}

void HostWindow::ensureStationCard(int stationId, const QString &stationName) {
    if (stationId <= 0 || !m_stationsGrid) return;
    if (m_lblNoStations) m_lblNoStations->setVisible(false);

    if (m_stationWidgets.contains(stationId)) {
        if (!stationName.isEmpty() && m_stationWidgets[stationId].lblTitle) {
            m_stationWidgets[stationId].lblTitle->setText(
                QString("🖥️ İSTASYON %1 (%2)").arg(stationId).arg(stationName));
        }
        return;
    }

    QFrame *card = new QFrame(m_stationsContainer);
    card->setMinimumWidth(260);
    card->setStyleSheet("background-color: #112D21; border: 1px solid #1A3D2B; border-radius: 6px; padding: 12px;");
    QVBoxLayout *cardVBox = new QVBoxLayout(card);
    cardVBox->setSpacing(8);

    QString titleStr = stationName.isEmpty()
                           ? QString("🖥️ İSTASYON %1").arg(stationId)
                           : QString("🖥️ İSTASYON %1 (%2)").arg(stationId).arg(stationName);
    QLabel *lblTitle = new QLabel(titleStr, card);
    lblTitle->setStyleSheet("font-size: 12pt; font-weight: bold; color: #34D399;");

    StationCardWidgets widgets;
    widgets.cardFrame = card;
    widgets.lblTitle = lblTitle;
    widgets.lblState = new QLabel("DURUM: BAĞLANDI (BOŞTA)", card);
    widgets.lblState->setStyleSheet(StyleHelper::getPastelBlueStyle());

    widgets.lblIp = new QLabel("IP Adresi: --", card);
    widgets.lblIp->setStyleSheet("color: #A7C4B5;");

    widgets.lblSn = new QLabel("Anten Barkodu: --", card);
    widgets.lblSn->setStyleSheet("font-weight: bold; color: #E6F4EE;");

    widgets.lblChannel = new QLabel("Aktif Kanal: --", card);
    widgets.lblChannel->setStyleSheet("color: #E6F4EE;");

    cardVBox->addWidget(widgets.lblTitle);
    cardVBox->addWidget(widgets.lblState);
    cardVBox->addWidget(widgets.lblIp);
    cardVBox->addWidget(widgets.lblSn);
    cardVBox->addWidget(widgets.lblChannel);
    cardVBox->addStretch();

    int idx = m_stationWidgets.size();
    int row = idx / 3;
    int col = idx % 3;
    m_stationsGrid->addWidget(card, row, col);
    m_stationWidgets[stationId] = widgets;
}

void HostWindow::setupGoldenPage() {
    m_goldenRefWidget = new GoldenReferenceWidget(&m_refWebClient, &m_refSerial, this);
    connect(m_goldenRefWidget, &GoldenReferenceWidget::referenceModeApplyRequested,
            this, &HostWindow::onReferenceModeApplyRequested);
    connect(m_goldenRefWidget, &GoldenReferenceWidget::logMessage,
            this, &HostWindow::appendLog);
    m_pageGolden = m_goldenRefWidget;
}

void HostWindow::setupNetworkPcsPage() {
    m_pageNetworkPcs = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageNetworkPcs);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(10);

    QGroupBox *grpTop = new QGroupBox("🖥️ Ağdaki (Sunucuya Katılan) PC'ler ve Canlı Durumları", m_pageNetworkPcs);
    QHBoxLayout *topRow = new QHBoxLayout(grpTop);
    topRow->setSpacing(10);

    m_lblPcSummaryStats = new QLabel("Toplam PC: 0  |  🟢 Çevrimiçi: 0  |  🧪 Aktif Testte: 0", grpTop);
    m_lblPcSummaryStats->setStyleSheet(StyleHelper::getPastelGreenStyle());

    QPushButton *btnRefresh = new QPushButton("🔄 Yenile", grpTop);
    btnRefresh->setCursor(Qt::PointingHandCursor);
    connect(btnRefresh, &QPushButton::clicked, this, &HostWindow::refreshNetworkPcsTable);

    QPushButton *btnClearOffline = new QPushButton("🧹 Çevrimdışı Olanları Temizle", grpTop);
    btnClearOffline->setCursor(Qt::PointingHandCursor);
    connect(btnClearOffline, &QPushButton::clicked, this, [this]() {
        for (auto it = m_knownStations.begin(); it != m_knownStations.end();) {
            if (!it.value().isConnected) {
                it = m_knownStations.erase(it);
            } else {
                ++it;
            }
        }
        refreshNetworkPcsTable();
    });

    topRow->addWidget(m_lblPcSummaryStats);
    topRow->addStretch();
    topRow->addWidget(btnRefresh);
    topRow->addWidget(btnClearOffline);
    vbox->addWidget(grpTop);

    m_tableNetworkPcs = new QTableWidget(m_pageNetworkPcs);
    m_tableNetworkPcs->setColumnCount(8);
    m_tableNetworkPcs->setHorizontalHeaderLabels({
        "İstasyon #", "PC / İstasyon Adı", "IP Adresi", "Bağlantı", "Çalışma Durumu", "Test Edilen Barkod", "Aktif Kanal", "Son Sinyal (Heartbeat)"
    });
    m_tableNetworkPcs->verticalHeader()->setVisible(false);
    m_tableNetworkPcs->setAlternatingRowColors(true);
    m_tableNetworkPcs->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableNetworkPcs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableNetworkPcs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableNetworkPcs->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_tableNetworkPcs->setColumnWidth(0, 88);
    m_tableNetworkPcs->setColumnWidth(2, 130);
    m_tableNetworkPcs->setColumnWidth(3, 120);
    m_tableNetworkPcs->setColumnWidth(5, 145);
    m_tableNetworkPcs->setColumnWidth(6, 95);
    m_tableNetworkPcs->setColumnWidth(7, 145);
    vbox->addWidget(m_tableNetworkPcs, 1);

    refreshNetworkPcsTable();
}

void HostWindow::refreshNetworkPcsTable() {
    if (!m_tableNetworkPcs) return;

    // Merge live server map with historical/disconnected stations
    const auto liveMap = m_tcpServer.connectedStations();
    for (auto it = liveMap.constBegin(); it != liveMap.constEnd(); ++it) {
        m_knownStations[it.key()] = it.value();
    }

    int totalCount = m_knownStations.size();
    int onlineCount = 0;
    int testingCount = 0;

    m_tableNetworkPcs->setRowCount(totalCount);
    int r = 0;
    for (auto it = m_knownStations.constBegin(); it != m_knownStations.constEnd(); ++it, ++r) {
        const auto &st = it.value();
        m_tableNetworkPcs->setRowHeight(r, 34);

        if (st.isConnected) onlineCount++;
        if (st.isConnected && st.currentState.contains("TEST", Qt::CaseInsensitive)) testingCount++;

        auto *itId = new QTableWidgetItem(QString("İstasyon %1").arg(st.stationId));
        itId->setTextAlignment(Qt::AlignCenter);

        auto *itName = new QTableWidgetItem(st.stationName.isEmpty() ? QString("İstasyon %1").arg(st.stationId) : st.stationName);

        auto *itIp = new QTableWidgetItem(st.ipAddress.isEmpty() ? "--" : st.ipAddress);
        itIp->setTextAlignment(Qt::AlignCenter);

        auto *itConn = new QTableWidgetItem(st.isConnected ? "🟢 ÇEVRİMİÇİ" : "🔴 BAĞLANTI YOK");
        itConn->setTextAlignment(Qt::AlignCenter);
        itConn->setForeground(QBrush(QColor(st.isConnected ? "#34D399" : "#F87171")));

        QString stateText = st.isConnected ? (st.currentState.isEmpty() ? "BOŞTA" : st.currentState) : "ÇEVRİMDIŞI";
        auto *itState = new QTableWidgetItem(stateText);
        itState->setTextAlignment(Qt::AlignCenter);
        if (!st.isConnected) {
            itState->setForeground(QBrush(QColor("#94A3B8")));
        } else if (stateText.contains("TEST", Qt::CaseInsensitive) || stateText.contains("KİLİT", Qt::CaseInsensitive)) {
            itState->setForeground(QBrush(QColor("#FBBF24")));
        } else {
            itState->setForeground(QBrush(QColor("#34D399")));
        }

        auto *itSn = new QTableWidgetItem(st.currentAntennaSn.isEmpty() ? "--" : st.currentAntennaSn);
        itSn->setTextAlignment(Qt::AlignCenter);

        auto *itCh = new QTableWidgetItem(st.currentChannel > 0 ? QString("Kanal %1").arg(st.currentChannel) : "--");
        itCh->setTextAlignment(Qt::AlignCenter);

        auto *itHb = new QTableWidgetItem(st.lastHeartbeat.isValid() ? st.lastHeartbeat.toString("dd.MM hh:mm:ss") : "--");
        itHb->setTextAlignment(Qt::AlignCenter);

        m_tableNetworkPcs->setItem(r, 0, itId);
        m_tableNetworkPcs->setItem(r, 1, itName);
        m_tableNetworkPcs->setItem(r, 2, itIp);
        m_tableNetworkPcs->setItem(r, 3, itConn);
        m_tableNetworkPcs->setItem(r, 4, itState);
        m_tableNetworkPcs->setItem(r, 5, itSn);
        m_tableNetworkPcs->setItem(r, 6, itCh);
        m_tableNetworkPcs->setItem(r, 7, itHb);
    }

    if (m_lblPcSummaryStats) {
        m_lblPcSummaryStats->setText(
            QString("Toplam PC: %1  |  🟢 Çevrimiçi: %2  |  🧪 Aktif Testte: %3")
                .arg(totalCount).arg(onlineCount).arg(testingCount));
    }
}

void HostWindow::setupHistoryPage() {
    m_pageHistory = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageHistory);
    vbox->setContentsMargins(0, 0, 0, 0);
    m_historyWidget = new TestHistoryWidget(m_pageHistory);
    connect(m_historyWidget, &TestHistoryWidget::logMessage, this, &HostWindow::appendLog);
    vbox->addWidget(m_historyWidget);
}

void HostWindow::setupOrdersPage() {
    m_pageOrders = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageOrders);
    vbox->setContentsMargins(10, 10, 10, 10);

    m_tableOrders = new QTableWidget(m_pageOrders);
    m_tableOrders->setColumnCount(7);
    m_tableOrders->setHorizontalHeaderLabels({"Sipariş No", "Müşteri", "Ürün Kodu", "Hedef Adet", "Tamamlanan", "Başarılı", "Durum"});
    m_tableOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableOrders->verticalHeader()->setVisible(false);
    m_tableOrders->setAlternatingRowColors(true);
    m_tableOrders->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableOrders->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vbox->addWidget(m_tableOrders);
}

void HostWindow::setupSettingsPage() {
    m_pageSettings = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageSettings);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(10);

    QGridLayout *cardsGrid = new QGridLayout();
    cardsGrid->setSpacing(10);

    // Card 1: Test Parameters
    QGroupBox *grpTestSettings = new QGroupBox("⚖️ Test ve Tolerans Parametreleri", m_pageSettings);
    QFormLayout *testForm = new QFormLayout(grpTestSettings);
    testForm->setSpacing(10);

    m_spinLockTimeout = new QSpinBox(grpTestSettings);
    m_spinLockTimeout->setRange(10, 600);
    m_spinLockTimeout->setSuffix(" sn");
    m_spinLockTimeout->setValue(AppConfig::instance().lockTimeoutSec());

    m_spinSampleDuration = new QSpinBox(grpTestSettings);
    m_spinSampleDuration->setRange(1, 30);
    m_spinSampleDuration->setSuffix(" sn");
    m_spinSampleDuration->setValue(AppConfig::instance().sampleDurationSec());

    m_spinTolerance = new QDoubleSpinBox(grpTestSettings);
    m_spinTolerance->setRange(0.1, 10.0);
    m_spinTolerance->setSingleStep(0.1);
    m_spinTolerance->setSuffix(" dB");
    m_spinTolerance->setValue(AppConfig::instance().defaultToleranceDb());

    m_spinHostTcpPort = new QSpinBox(grpTestSettings);
    m_spinHostTcpPort->setRange(1024, 65535);
    m_spinHostTcpPort->setValue(AppConfig::instance().tcpPort());

    m_spinHostUdpPort = new QSpinBox(grpTestSettings);
    m_spinHostUdpPort->setRange(1024, 65535);
    m_spinHostUdpPort->setValue(AppConfig::instance().udpBeaconPort());

    QPushButton *btnSave = new QPushButton("💾 Ayarları Kaydet", grpTestSettings);
    btnSave->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold;");
    connect(btnSave, &QPushButton::clicked, this, &HostWindow::onSaveSettingsClicked);

    testForm->addRow("Maksimum Kilit Zaman Aşımı:", m_spinLockTimeout);
    testForm->addRow("Kanal Başına Örnekleme Süresi:", m_spinSampleDuration);
    testForm->addRow("Varsayılan Tolerans Payı:", m_spinTolerance);
    testForm->addRow("TCP Sunucu Portu:", m_spinHostTcpPort);
    testForm->addRow("UDP Lobi Yayın Portu:", m_spinHostUdpPort);
    testForm->addRow(btnSave);
    cardsGrid->addWidget(grpTestSettings, 0, 0);

    // Card 2: Reference DRX Ethernet (Primary)
    QGroupBox *grpRefEth = new QGroupBox("🌐 Referans DRX9000 Ethernet & Web CGI Ayarları", m_pageSettings);
    QFormLayout *refForm = new QFormLayout(grpRefEth);
    refForm->setSpacing(10);

    m_txtRefDrxIp = new QLineEdit(AppConfig::instance().refDrxHost(), grpRefEth);
    m_spinRefDrxPort = new QSpinBox(grpRefEth);
    m_spinRefDrxPort->setRange(1, 65535);
    m_spinRefDrxPort->setValue(AppConfig::instance().refDrxPort());
    m_txtRefDrxUser = new QLineEdit(AppConfig::instance().drxUsername(), grpRefEth);
    m_txtRefDrxPass = new QLineEdit(AppConfig::instance().drxPassword(), grpRefEth);
    m_txtRefDrxPass->setEchoMode(QLineEdit::Password);

    connect(m_txtRefDrxIp, &QLineEdit::editingFinished, this, [this]() {
        const QString ip = m_txtRefDrxIp->text().trimmed();
        if (ip.isEmpty()) return;
        AppConfig &cfg = AppConfig::instance();
        cfg.setRefDrxHost(ip);
        cfg.save();
        m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
        if (m_txtHeaderRefIp) {
            m_txtHeaderRefIp->blockSignals(true);
            m_txtHeaderRefIp->setText(ip);
            m_txtHeaderRefIp->blockSignals(false);
        }
    });

    QPushButton *btnSaveRefEth = new QPushButton("🔗 Kaydet & Ref Ping Testi Yap", grpRefEth);
    btnSaveRefEth->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnSaveRefEth, &QPushButton::clicked, this, &HostWindow::onSaveRefWebSettingsClicked);

    refForm->addRow("Referans (Golden) DRX IP:", m_txtRefDrxIp);
    refForm->addRow("Referans DRX Port:", m_spinRefDrxPort);
    refForm->addRow("Web Kullanıcı Adı:", m_txtRefDrxUser);
    refForm->addRow("Web Şifresi:", m_txtRefDrxPass);
    refForm->addRow(btnSaveRefEth);
    cardsGrid->addWidget(grpRefEth, 0, 1);

    // Card 3: Database Management
    QGroupBox *grpDbSettings = new QGroupBox("🗄️ Merkezi Veritabanı & MariaDB Servisi", m_pageSettings);
    QVBoxLayout *vboxDb = new QVBoxLayout(grpDbSettings);
    vboxDb->setSpacing(10);

    QHBoxLayout *statusRow = new QHBoxLayout();
    QLabel *lblDbStatusTitle = new QLabel("Servis Durumu:", grpDbSettings);
    m_lblDbStatus = new QLabel("Kontrol ediliyor...", grpDbSettings);
    m_lblDbStatus->setStyleSheet("font-weight: bold; color: #FBBF24;");
    QPushButton *btnCheckDb = new QPushButton("🔄 Kontrol Et", grpDbSettings);
    connect(btnCheckDb, &QPushButton::clicked, this, &HostWindow::onCheckDbClicked);
    statusRow->addWidget(lblDbStatusTitle);
    statusRow->addWidget(m_lblDbStatus);
    statusRow->addStretch();
    statusRow->addWidget(btnCheckDb);
    vboxDb->addLayout(statusRow);

    QFormLayout *dbForm = new QFormLayout();
    m_txtDbHost = new QLineEdit(AppConfig::instance().dbHost(), grpDbSettings);
    m_spinDbPort = new QSpinBox(grpDbSettings);
    m_spinDbPort->setRange(1, 65535);
    m_spinDbPort->setValue(AppConfig::instance().dbPort());
    m_txtDbName = new QLineEdit(AppConfig::instance().dbName(), grpDbSettings);
    dbForm->addRow("Sunucu Host IP:", m_txtDbHost);
    dbForm->addRow("Veritabanı Portu:", m_spinDbPort);
    dbForm->addRow("Veritabanı Adı:", m_txtDbName);
    vboxDb->addLayout(dbForm);

    QHBoxLayout *dbActionRow = new QHBoxLayout();
    QPushButton *btnInstallMysql = new QPushButton("⚡ MariaDB Servisini Başlat / Kur", grpDbSettings);
    btnInstallMysql->setStyleSheet("background-color: #D97706; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnInstallMysql, &QPushButton::clicked, this, &HostWindow::onInstallMysqlClicked);

    QPushButton *btnResetDb = new QPushButton("📦 Şemayı / Veriyi Yenile", grpDbSettings);
    connect(btnResetDb, &QPushButton::clicked, this, &HostWindow::onResetDbClicked);

    dbActionRow->addWidget(btnInstallMysql);
    dbActionRow->addWidget(btnResetDb);
    vboxDb->addLayout(dbActionRow);
    cardsGrid->addWidget(grpDbSettings, 1, 0);

    // Card 4: Optional Legacy RS232 Fallback for Reference DRX
    QGroupBox *grpRefSerial = new QGroupBox("🔧 Gelişmiş: Referans RS232 Yedek Seri Port (Opsiyonel)", m_pageSettings);
    QFormLayout *serForm = new QFormLayout(grpRefSerial);
    serForm->setSpacing(10);

    QLabel *lblSerNote = new QLabel("Birincil iletişim Ethernet (CAT6) Web CGI üzerindendir. Sadece acil debug için RS232 seçin.", grpRefSerial);
    lblSerNote->setWordWrap(true);
    lblSerNote->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");

    QHBoxLayout *serCtrlRow = new QHBoxLayout();
    m_comboSerialPorts = new QComboBox(grpRefSerial);
    QPushButton *btnRefresh = new QPushButton("🔄", grpRefSerial);
    btnRefresh->setFixedSize(32, 28);
    connect(btnRefresh, &QPushButton::clicked, this, &HostWindow::onRefreshPortsClicked);
    m_btnConnectRefSerial = new QPushButton("RS232 Bağlan", grpRefSerial);
    m_btnConnectRefSerial->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnConnectRefSerial, &QPushButton::clicked, this, &HostWindow::onConnectRefSerialClicked);

    serCtrlRow->addWidget(m_comboSerialPorts, 1);
    serCtrlRow->addWidget(btnRefresh);
    serCtrlRow->addWidget(m_btnConnectRefSerial);

    serForm->addRow(lblSerNote);
    serForm->addRow("COM Port Seçimi:", serCtrlRow);
    cardsGrid->addWidget(grpRefSerial, 1, 1);

    vbox->addLayout(cardsGrid);
    vbox->addStretch();

    // Anında QSettings (.ini) otomatik kayıt bağlantıları
    auto autoSaveHostSettings = [this]() {
        AppConfig &cfg = AppConfig::instance();
        cfg.setLockTimeoutSec(m_spinLockTimeout->value());
        cfg.setSampleDurationSec(m_spinSampleDuration->value());
        cfg.setDefaultToleranceDb(m_spinTolerance->value());
        cfg.setRefDrxHost(m_txtRefDrxIp->text().trimmed());
        cfg.setDrxUsername(m_txtRefDrxUser->text().trimmed());
        cfg.setDrxPassword(m_txtRefDrxPass->text());
        cfg.setDbHost(m_txtDbHost->text().trimmed());
        cfg.setDbName(m_txtDbName->text().trimmed());
        cfg.save();
    };
    connect(m_spinLockTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, autoSaveHostSettings);
    connect(m_spinSampleDuration, QOverload<int>::of(&QSpinBox::valueChanged), this, autoSaveHostSettings);
    connect(m_spinTolerance, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, autoSaveHostSettings);
    connect(m_txtRefDrxIp, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtRefDrxUser, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtRefDrxPass, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtDbHost, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtDbName, &QLineEdit::editingFinished, this, autoSaveHostSettings);

    onCheckDbClicked();
}

void HostWindow::setupLogsPage() {
    m_pageLogs = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageLogs);
    vbox->setContentsMargins(10, 10, 10, 10);

    QGroupBox *grpLogs = new QGroupBox("📜 Sunucu, Hakem (Mutex Arbiter) ve Referans Ethernet Logları", m_pageLogs);
    QVBoxLayout *logBox = new QVBoxLayout(grpLogs);

    m_txtLog = new QTextEdit(grpLogs);
    m_txtLog->setReadOnly(true);
    m_txtLog->setStyleSheet("background-color: #040B09; color: #34D399; font-family: Consolas, 'Courier New', monospace; font-size: 9.5pt;");
    logBox->addWidget(m_txtLog);

    vbox->addWidget(grpLogs);
}

void HostWindow::onRefEthPingSyncClicked() {
    if (m_txtHeaderRefIp) {
        const QString enteredIp = m_txtHeaderRefIp->text().trimmed();
        AppConfig &cfg = AppConfig::instance();
        if (!enteredIp.isEmpty() && enteredIp != cfg.refDrxHost()) {
            cfg.setRefDrxHost(enteredIp);
            cfg.save();
            m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
            if (m_txtRefDrxIp) {
                m_txtRefDrxIp->setText(enteredIp);
            }
        }
    }
    m_lblRefPingBadge->setText("📶 Ref Ping: Aranıyor...");
    m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());
    m_refWebClient.testConnectionAndSync();
}

void HostWindow::onRefPingFinished(bool reachable, int latencyMs, const QString &hostIp) {
    if (reachable) {
        m_lblRefPingBadge->setText(QString("📶 Ref Ping: %1 ms (OK)").arg(latencyMs));
        m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());

        if (!m_refWebClient.isLoggedIn()) {
            m_lblRefTelemetry->setText("🟡 Ref Web: Oturum Açılıyor...");
            m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelAmberStyle());
        }
    } else {
        m_lblRefPingBadge->setText("❌ Ref Ping: Yanıt Yok");
        m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelRedStyle());

        m_lblRefTelemetry->setText(QString("🔴 Ref ETH: Yanıt Yok (%1)").arg(hostIp));
        m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelRedStyle());
    }
}

void HostWindow::onRefWebLoginSucceeded(int sessionKey) {
    m_lblRefTelemetry->setText(QString("🟢 Ref Web: Hazır (Key: %1)").arg(sessionKey));
    m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelGreenStyle());
    setStatusMessage(QString("Referans DRX Ethernet hazır (%1, SessionKey %2)").arg(m_refWebClient.host()).arg(sessionKey));
}

void HostWindow::onRefWebLoginFailed(const QString &reason) {
    m_lblRefTelemetry->setText("🔴 Ref Web Login Hata");
    m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelRedStyle());
    appendLog("[REF ETH HATA] Web CGI login başarısız: " + reason);
}

void HostWindow::onSaveRefWebSettingsClicked() {
    AppConfig &cfg = AppConfig::instance();
    cfg.setRefDrxHost(m_txtRefDrxIp->text().trimmed());
    if (m_spinRefDrxPort) cfg.setRefDrxPort(static_cast<quint16>(m_spinRefDrxPort->value()));
    cfg.setDrxUsername(m_txtRefDrxUser->text().trimmed());
    cfg.setDrxPassword(m_txtRefDrxPass->text());
    cfg.save();

    m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
    m_refWebClient.setCredentials(cfg.drxUsername(), cfg.drxPassword());
    if (m_txtHeaderRefIp) {
        m_txtHeaderRefIp->setText(cfg.refDrxHost());
    }

    onRefEthPingSyncClicked();
    QMessageBox::information(this, "Güncellendi", "Referans DRX Ethernet ayarları kaydedildi ve Ping testi başlatıldı.");
}

void HostWindow::onRefreshPortsClicked() {
    if (!m_comboSerialPorts) return;
    m_comboSerialPorts->clear();
    QStringList ports = SerialWorker::availablePorts();
    m_comboSerialPorts->addItems(ports);

    int idx = m_comboSerialPorts->findText(AppConfig::instance().serialPort());
    if (idx >= 0) m_comboSerialPorts->setCurrentIndex(idx);
}

void HostWindow::onConnectRefSerialClicked() {
    if (m_refSerial.isConnected()) {
        m_refSerial.closePort();
    } else if (m_comboSerialPorts) {
        QString port = m_comboSerialPorts->currentText();
        if (port.isEmpty()) {
            QMessageBox::warning(this, "Uyarı", "Lütfen bir seri port seçiniz!");
            return;
        }
        AppConfig::instance().setSerialPort(port);
        AppConfig::instance().save();
        m_refSerial.openPort(port, AppConfig::instance().serialBaudRate());
    }
}

void HostWindow::onStationConnected(int stationId, const QString &ip) {
    ensureStationCard(stationId);
    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: BAĞLANDI (BOŞTA)");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelBlueStyle());
        m_stationWidgets[stationId].lblIp->setText("IP Adresi: " + ip);
    }
    m_knownStations[stationId].stationId = stationId;
    m_knownStations[stationId].ipAddress = ip;
    m_knownStations[stationId].isConnected = true;
    m_knownStations[stationId].currentState = "BOŞTA";
    m_knownStations[stationId].lastHeartbeat = QDateTime::currentDateTime();
    refreshNetworkPcsTable();

    m_beaconBroadcaster.setStationCount(m_tcpServer.connectedStations().size());
    appendLog(QString("İstasyon %1 ağa bağlandı (%2)").arg(stationId).arg(ip));
}

void HostWindow::onStationDisconnected(int stationId) {
    if (m_stationWidgets.contains(stationId)) {
        StationCardWidgets widgets = m_stationWidgets.take(stationId);
        if (widgets.cardFrame) {
            if (m_stationsGrid) {
                m_stationsGrid->removeWidget(widgets.cardFrame);
            }
            widgets.cardFrame->deleteLater();
        }
        // Re-pack remaining station cards cleanly so no gaps remain in the 3-column grid
        if (m_stationsGrid) {
            int idx = 0;
            for (auto it = m_stationWidgets.begin(); it != m_stationWidgets.end(); ++it) {
                if (it.value().cardFrame) {
                    m_stationsGrid->removeWidget(it.value().cardFrame);
                    m_stationsGrid->addWidget(it.value().cardFrame, idx / 3, idx % 3);
                    ++idx;
                }
            }
        }
    }
    if (m_stationWidgets.isEmpty() && m_lblNoStations) {
        m_lblNoStations->setVisible(true);
    }

    m_knownStations.remove(stationId);
    refreshNetworkPcsTable();

    m_beaconBroadcaster.setStationCount(m_tcpServer.connectedStations().size());
    appendLog(QString("İstasyon %1 ağdan ayrıldı.").arg(stationId));
}

void HostWindow::onStationStatusUpdated(const StationLiveStatus &status) {
    int stId = status.stationId;
    ensureStationCard(stId, status.stationName);
    if (m_stationWidgets.contains(stId)) {
        m_stationWidgets[stId].lblSn->setText(status.currentAntennaSn.isEmpty() ? "Anten Barkodu: --" : "Anten Barkodu: " + status.currentAntennaSn);
        m_stationWidgets[stId].lblChannel->setText(status.currentChannel > 0 ? QString("Aktif Kanal: %1").arg(status.currentChannel) : "Aktif Kanal: --");
    }
    m_knownStations[stId] = status;
    refreshNetworkPcsTable();
}

void HostWindow::onLockGranted(int stationId, int timeoutSec) {
    m_lblArbiterStatus->setText(QString("🔒 KİLİTLİ: İstasyon %1 Test Yapıyor (Süre Sınırı: %2 sn)").arg(stationId).arg(timeoutSec));
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #FBBF24;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: TESTTE (REFERANS KİLİTLİ)");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelAmberStyle());
    }
    if (m_knownStations.contains(stationId)) {
        m_knownStations[stationId].currentState = "TESTTE (REFERANS KİLİTLİ)";
        refreshNetworkPcsTable();
    }
}

void HostWindow::onLockReleased(int stationId) {
    m_lblArbiterStatus->setText("🟢 Referans Alıcısı BOŞTA (Tüm İstasyonlar İçin Müsait)");
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: BOŞTA");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelGreenStyle());
    }
    if (m_knownStations.contains(stationId)) {
        m_knownStations[stationId].currentState = "BOŞTA";
        refreshNetworkPcsTable();
    }
}

void HostWindow::onLockExpired(int stationId) {
    appendLog(QString("UYARI: İstasyon %1 için kilit süresi aşıldı ve düşürüldü!").arg(stationId));
    onLockReleased(stationId);
}

void HostWindow::onRefreshHistoryClicked() {
    if (m_historyWidget) {
        m_historyWidget->refreshHistory();
    }

    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        db.connectToDatabase(AppConfig::instance().dbHost(), AppConfig::instance().dbPort(),
                             AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                             AppConfig::instance().dbPassword());
    }

    auto orders = db.getActiveWorkOrders();
    m_tableOrders->setRowCount(orders.size());
    for (int i = 0; i < orders.size(); ++i) {
        const auto &o = orders[i];
        m_tableOrders->setItem(i, 0, new QTableWidgetItem(o.orderNo));
        m_tableOrders->setItem(i, 1, new QTableWidgetItem(o.customerName));
        m_tableOrders->setItem(i, 2, new QTableWidgetItem(o.productCode));
        m_tableOrders->setItem(i, 3, new QTableWidgetItem(QString::number(o.targetQuantity)));
        m_tableOrders->setItem(i, 4, new QTableWidgetItem(QString::number(o.completedQuantity)));
        m_tableOrders->setItem(i, 5, new QTableWidgetItem(QString::number(o.passedQuantity)));
        m_tableOrders->setItem(i, 6, new QTableWidgetItem(o.status));
    }
}

void HostWindow::onSaveSettingsClicked() {
    AppConfig &cfg = AppConfig::instance();
    const quint16 oldTcpPort = cfg.tcpPort();

    cfg.setLockTimeoutSec(m_spinLockTimeout->value());
    cfg.setSampleDurationSec(m_spinSampleDuration->value());
    cfg.setDefaultToleranceDb(m_spinTolerance->value());
    if (m_spinHostTcpPort) cfg.setTcpPort(static_cast<quint16>(m_spinHostTcpPort->value()));
    if (m_spinHostUdpPort) cfg.setUdpBeaconPort(static_cast<quint16>(m_spinHostUdpPort->value()));
    if (m_txtDbHost) cfg.setDbHost(m_txtDbHost->text().trimmed());
    if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
    if (m_txtDbName) cfg.setDbName(m_txtDbName->text().trimmed());
    cfg.save();

    if (m_spinHostTcpPort && cfg.tcpPort() != oldTcpPort) {
        m_tcpServer.stop();
        m_tcpServer.start(cfg.tcpPort());
        m_lblServerStatus->setText(QString("🟢 TCP Sunucu Aktif (Port %1)").arg(cfg.tcpPort()));
    }

    QMessageBox::information(this, "Kaydedildi", "Sistem parametreleri ve ağ/port ayarları başarıyla güncellendi.");
}

void HostWindow::onInstallMysqlClicked() {
    DependencyInstaller::autoSetupHostEnvironment(this);
    onCheckDbClicked();
    onRefreshHistoryClicked();
}

void HostWindow::onResetDbClicked() {
    DependencyInstaller::runResetWithElevation(this);
    DbManager::instance().ensureDefaultDataIfMissing();
    if (m_goldenRefWidget) m_goldenRefWidget->reloadFrequencies();
    onCheckDbClicked();
    onRefreshHistoryClicked();
}

void HostWindow::onCheckDbClicked() {
    bool listening = DependencyInstaller::isMySqlListening();
    if (m_lblDbStatus) {
        if (listening) {
            m_lblDbStatus->setText("✔ MySQL / MariaDB Aktif (Port 3306)");
            m_lblDbStatus->setStyleSheet("color: #34D399; font-weight: bold;");
        } else {
            m_lblDbStatus->setText("❌ Servis Kapalı / Dinlenmiyor");
            m_lblDbStatus->setStyleSheet("color: #F87171; font-weight: bold;");
        }
    }
}

void HostWindow::appendLog(const QString &msg) {
    AppLogger::instance().logEvent("HOST", msg);
    if (!m_txtLog) return;
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_txtLog->append(QString("[%1] %2").arg(timeStr, msg));
}

void HostWindow::setStatusMessage(const QString &msg) {
    if (m_statusLabel) {
        m_statusLabel->setText("✔ " + msg);
    }
}

void HostWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}

void HostWindow::onLogoutClicked() {
    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        m_lblOperatorName->setText(AppConfig::instance().currentUserFullName());
        setStatusMessage("Yönetici oturumu yenilendi: " + AppConfig::instance().currentUserFullName());
    }
}

void HostWindow::onReturnLobbyClicked() {
    m_returningToLobby = true;
    shutdownHostServices();
    LobbyWindow *lobbyWin = new LobbyWindow();
    lobbyWin->show();
    this->close();
}

void HostWindow::updateRefModeBadgeUi(ReferenceMode mode) {
    if (!m_lblRefModeBadge) return;
    if (mode == ReferenceMode::GoldenSample) {
        m_lblRefModeBadge->setText("🌟 Mod: Golden Sample (Kayıtlı)");
        m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());
    } else {
        m_lblRefModeBadge->setText("🟢 Mod: Canlı Referans");
        m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());
    }
}

void HostWindow::onReferenceModeApplyRequested(ReferenceMode mode) {
    AppConfig::instance().setLastReferenceMode(mode);
    DbManager::instance().setReferenceMode(mode);
    m_tcpServer.setReferenceMode(mode);
    updateRefModeBadgeUi(mode);

    QString modeDesc = (mode == ReferenceMode::GoldenSample)
                           ? "Kayıtlı Golden Sample (Referans Anten Olmadan)"
                           : "Canlı Referans Anten (Live Reference)";
    setStatusMessage("Aktif referans modu güncellendi: " + modeDesc);
    QMessageBox::information(this, "Referans Modu Güncellendi",
                             QString("Tüm test istasyonları için aktif referans modu güncellendi:\n\n• %1").arg(modeDesc));
}
