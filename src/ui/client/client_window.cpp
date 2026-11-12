/**
 * @file client_window.cpp
 * @brief Implementation of the Ethernet-first DRX-inspired sidebar-navigated operator and engineer interface.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "client_window.h"
#include "appinfo.h"
#include "app_config.h"
#include "build_config.h"
#include "core/app_logger.h"
#include "core/simulator_launcher.h"
#include "database/db_manager.h"
#include "ui/common/style_helper.h"
#include "ui/common/about_dialog.h"
#include "ui/common/login_dialog.h"
#include "ui/common/lobby_window.h"
#include "ui/host/host_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QMessageBox>
#include <QDateTime>
#include <QPixmap>
#include <QtMath>
#include <QSplitter>
#include <QFrame>

ClientWindow::ClientWindow(QWidget *parent)
    : QMainWindow(parent), m_beaconListener(AppConfig::instance().udpBeaconPort()) {

    setAttribute(Qt::WA_DeleteOnClose);

#if defined(NETA_ALGORITMA_1_FOUR_PC)
    QString algoTitle = "[4 PC - Kilitli Kuyruk]";
#elif defined(NETA_ALGORITMA_2_THREE_PC)
    QString algoTitle = "[3 PC - DB Kuyruk]";
#else
    QString algoTitle = "";
#endif

    setWindowTitle(QString("%1 - İstasyon %2 %3 v%4")
                       .arg(NETA_APP_TITLE)
                       .arg(AppConfig::instance().stationId())
                       .arg(algoTitle)
                       .arg(NETA_APP_VERSION));
    setMinimumSize(1180, 740);
    setWindowState(Qt::WindowMaximized);

    m_stateMachine = new TestStateMachine(&m_dutSerial, &m_netClient, this);

    // Primary CAT6 Ethernet Web CGI Client for DUT receiver
    m_dutWebClient.setHost(AppConfig::instance().stationDrxHost(), AppConfig::instance().stationDrxPort());
    m_dutWebClient.setCredentials(AppConfig::instance().drxUsername(), AppConfig::instance().drxPassword());
    connect(&m_dutWebClient, &DrxWebClient::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog((isErr ? "[DRX ETH HATA] " : "[DRX ETH] ") + msg);
    });
    connect(&m_dutWebClient, &DrxWebClient::pingFinished, this, &ClientWindow::onEthPingFinished);
    connect(&m_dutWebClient, &DrxWebClient::loginSucceeded, this, &ClientWindow::onWebLoginSucceeded);
    connect(&m_dutWebClient, &DrxWebClient::loginFailed, this, &ClientWindow::onWebLoginFailed);
    connect(&m_dutWebClient, &DrxWebClient::deviceInfoReceived, this, &ClientWindow::onWebDeviceInfoReceived);
    connect(&m_dutWebClient, &DrxWebClient::statusReceived, this,
            [this](bool sig, bool vid, bool /*aud*/, const QString &ch) {
        if (!ch.isEmpty() && m_lblLiveChannel) {
            m_lblLiveChannel->setText("📺 Kanal: " + ch);
        }
        if (m_lblStatusSignal) {
            m_lblStatusSignal->setText(sig ? "📡 Sinyal: KİLİTLİ" : "📡 Sinyal: YOK");
            m_lblStatusSignal->setStyleSheet(sig ? StyleHelper::getPastelGreenStyle() : StyleHelper::getPastelRedStyle());
        }
        if (m_lblStatusVideo) {
            m_lblStatusVideo->setText(vid ? "🎬 Video: VAR" : "🎬 Video: YOK");
            m_lblStatusVideo->setStyleSheet(vid ? StyleHelper::getPastelGreenStyle() : StyleHelper::getPastelRedStyle());
        }
    });
    connect(&m_dutWebClient, &DrxWebClient::telemetryReceived, this,
            [this](double powDbm, double cnDb, double /*ber*/, double /*ebN0*/) {
        if (m_lblLiveDutCn) m_lblLiveDutCn->setText(QString("📊 DUT C/N: %1 dB").arg(cnDb, 0, 'f', 1));
        if (m_lblLivePower) m_lblLivePower->setText(QString("📶 POW: %1 dBm").arg(powDbm, 0, 'f', 1));
    });

    // Reference Receiver Web CGI Client (used directly in Algorithm 2)
    m_refWebClient.setHost(AppConfig::instance().refDrxHost(), AppConfig::instance().refDrxPort());
    m_refWebClient.setCredentials(AppConfig::instance().drxUsername(), AppConfig::instance().drxPassword());
    connect(&m_refWebClient, &DrxWebClient::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog((isErr ? "[REF ETH HATA] " : "[REF ETH] ") + msg);
    });

    m_stateMachine->setWebClients(&m_dutWebClient, &m_refWebClient);

    setupMenuBar();
    setupUi();
    setupPlot();

    // Network & Beacon Signal Wiring
    connect(&m_beaconListener, &UdpBeaconListener::hostDiscovered, this, &ClientWindow::onHostDiscovered);
    connect(&m_netClient, &TcpClient::connected, this, &ClientWindow::onTcpConnected);
    connect(&m_netClient, &TcpClient::disconnected, this, &ClientWindow::onTcpDisconnected);
    connect(&m_netClient, &TcpClient::serverShutdown, this, [this]() {
        m_lblConnectionStatus->setText("🔴 Host: Sunucu Kapatıldı");
        m_lblConnectionStatus->setStyleSheet(StyleHelper::getPastelRedStyle());
        m_lblLockBanner->setText("⚪ Kilit: Sunucu Kapalı");
        m_lblLockBanner->setStyleSheet(StyleHelper::getPastelSlateStyle());
        setStatusMessage("Yönetici Sunucu kapatıldı. Tekrar açıldığında bağlanabilirsiniz.");
        appendLog("[AĞ UYARI] Yönetici Sunucu (Host) kapatıldı!");
        QMessageBox::warning(this, "Yönetici Sunucu Kapatıldı",
                             "Yönetici PC sunucuyu kapattı.\n\n"
                             "Yönetici tekrar sunucuyu başlattığında 'Sunucuya Yeniden Bağlan' veya Lobi üzerinden bağlanabilirsiniz.");
    });
    connect(&m_netClient, &TcpClient::userAuthorizationReceived, this, [this](const QString &role, const QString &fullName, bool isActive) {
        if (!isActive) {
            if (m_btnStartTest) m_btnStartTest->setEnabled(false);
            appendLog("[YETKİ UYARI] Hesabınız Yönetici PC tarafından pasifleştirildi!");
            QMessageBox::warning(this, "Hesap Pasif", "Kullanıcı hesabınız Yönetici tarafından pasif duruma getirildi.");
            return;
        }
        AppConfig &cfg = AppConfig::instance();
        const UserRole newRole = (role.compare("ADMIN", Qt::CaseInsensitive) == 0)
                                     ? UserRole::Engineer
                                     : UserRole::Technician;
        const bool roleChanged = (cfg.userRole() != newRole);
        cfg.setUserRole(newRole);
        if (!fullName.isEmpty()) {
            cfg.setCurrentUserFullName(fullName);
        }
        if (m_btnStartTest) m_btnStartTest->setEnabled(true);
        applyUserRoleUi();
        if (roleChanged) {
            appendLog(QString("[Yetkilendirme] Yönetici PC yetkinizi güncelledi: %1")
                          .arg(newRole == UserRole::Engineer ? "Mühendis (ADMIN)" : "Tekniker (OPERATOR)"));
        }
    });
    connect(&m_netClient, &TcpClient::stationIdAssigned, this, &ClientWindow::onStationIdAssigned);
    connect(&m_netClient, &TcpClient::referenceModeChanged, this, &ClientWindow::onReferenceModeChanged);
    connect(&m_netClient, &TcpClient::logMessage, this, &ClientWindow::appendLog);

    // Optional Legacy RS232 Fallback Wiring (controls live inside Engineer Settings tab only)
    connect(&m_dutSerial, &SerialWorker::portOpened, this, [this](const QString &p) {
        if (m_btnConnectSerial) {
            m_btnConnectSerial->setText("RS232 Bağlantıyı Kes");
            m_btnConnectSerial->setStyleSheet("background-color: #B91C1C; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
        }
        appendLog("[RS232 Yedek] Seri port açıldı: " + p);
    });
    connect(&m_dutSerial, &SerialWorker::portClosed, this, [this]() {
        if (m_btnConnectSerial) {
            m_btnConnectSerial->setText("RS232 Bağlan");
            m_btnConnectSerial->setStyleSheet("background-color: #0284C7; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
        }
        appendLog("[RS232 Yedek] Seri port kapatıldı.");
    });
    connect(&m_dutSerial, &SerialWorker::statusReceived, this, [this](bool sig, bool vid, bool /*aud*/) {
        if (m_lblStatusSignal) {
            m_lblStatusSignal->setText(sig ? "📡 Sinyal: KİLİTLİ" : "📡 Sinyal: YOK");
            m_lblStatusSignal->setStyleSheet(sig ? StyleHelper::getPastelGreenStyle() : StyleHelper::getPastelRedStyle());
        }
        if (m_lblStatusVideo) {
            m_lblStatusVideo->setText(vid ? "🎬 Video: VAR" : "🎬 Video: YOK");
            m_lblStatusVideo->setStyleSheet(vid ? StyleHelper::getPastelGreenStyle() : StyleHelper::getPastelRedStyle());
        }
    });
    connect(&m_dutSerial, &SerialWorker::logMessage, this, &ClientWindow::appendLog);

    // State Machine Signal Wiring
    connect(m_stateMachine, &TestStateMachine::stateChanged, this, &ClientWindow::onStateChanged);
    connect(m_stateMachine, &TestStateMachine::channelStarted, this, &ClientWindow::onChannelStarted);
    connect(m_stateMachine, &TestStateMachine::channelSampleUpdated, this, &ClientWindow::onChannelSampleUpdated);
    connect(m_stateMachine, &TestStateMachine::channelCompleted, this, &ClientWindow::onChannelCompleted);
    connect(m_stateMachine, &TestStateMachine::testFinished, this, &ClientWindow::onTestFinished);
    connect(m_stateMachine, &TestStateMachine::waitingForLockUpdate, this, &ClientWindow::onWaitingForLockUpdate);
    connect(m_stateMachine, &TestStateMachine::logMessage, this, &ClientWindow::appendLog);

    // Background Ethernet Ping Heartbeat (8 seconds, silent; full sync on reconnect or manual press)
    connect(&m_ethPingTimer, &QTimer::timeout, this, [this]() {
        m_dutWebClient.pingHost(1000);
    });
    m_ethPingTimer.start(8000);

    // UDP Beacon & Host Auto-Connect (Architecture 1)
    m_beaconListener.start();
    appendLog("Lobi keşfi dinleniyor (Port " + QString::number(AppConfig::instance().udpBeaconPort()) + ")...");

#if defined(NETA_ALGORITMA_1_FOUR_PC)
    if (!AppConfig::instance().hostIp().isEmpty()) {
        m_netClient.connectToHost(AppConfig::instance().hostIp(), AppConfig::instance().tcpPort());
    }
#endif

    loadWorkOrdersAndProfiles();
    onReferenceModeChanged(DbManager::instance().getReferenceMode());
    onRefreshPortsClicked();
    applyUserRoleUi();

    // Immediate startup sequence: ICMP Ping -> Web CGI Login -> DeviceInfo & AlarmStatus Data Pull
    QTimer::singleShot(250, this, &ClientWindow::onEthPingSyncClicked);
    setStatusMessage(QString("Başlatılıyor | İstasyon %1 | DRX Ethernet Ping doğrulanıyor...").arg(AppConfig::instance().stationId()));
}

ClientWindow::~ClientWindow() {
    AppConfig::instance().save();
    m_ethPingTimer.stop();
    m_beaconListener.stop();
    m_dutSerial.closePort();
}

void ClientWindow::setupMenuBar() {
    QMenu *menuOturum = menuBar()->addMenu("Oturum");
    QAction *actLobby = menuOturum->addAction("🏠 Sunucu Lobisine Dön (HOST / JOIN)");
    connect(actLobby, &QAction::triggered, this, &ClientWindow::onReturnLobbyClicked);

    QAction *actLogout = menuOturum->addAction("🔄 Kullanıcı Değiştir");
    actLogout->setShortcut(QKeySequence("Ctrl+L"));
    connect(actLogout, &QAction::triggered, this, &ClientWindow::onLogoutClicked);

    menuOturum->addSeparator();
    QAction *actQuit = menuOturum->addAction("Programı Kapat");
    actQuit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    QMenu *menuBaglanti = menuBar()->addMenu("Bağlantı");
    QAction *actEthSync = menuBaglanti->addAction("Ethernet Ping Testi & DRX Veri Yenile");
    actEthSync->setShortcut(QKeySequence("Ctrl+P"));
    connect(actEthSync, &QAction::triggered, this, &ClientWindow::onEthPingSyncClicked);

    menuBaglanti->addSeparator();
    QAction *actHostReconnect = menuBaglanti->addAction("Yönetici Sunucusuna (Host) Bağlan");
    connect(actHostReconnect, &QAction::triggered, this, &ClientWindow::onReconnectHostClicked);

    QMenu *menuAyarlar = menuBar()->addMenu("Ayarlar");
    QAction *actAbout = menuAyarlar->addAction("Hakkında");
    actAbout->setShortcut(QKeySequence("Alt+A"));
    connect(actAbout, &QAction::triggered, this, &ClientWindow::onAboutClicked);

    // Top-right quick Simulator launcher button (always accessible across all tabs)
    auto *btnLaunchSim = new QPushButton("🧪 Simülatörü Aç", this);
    btnLaunchSim->setCursor(Qt::PointingHandCursor);
    btnLaunchSim->setToolTip("NETA DRX9000 TCP Simülatörünü başlat");
    btnLaunchSim->setStyleSheet(
        "QPushButton { background-color: #1D4ED8; color: #FFFFFF; border: none; border-radius: 4px; "
        "padding: 4px 12px; margin: 2px 6px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2563EB; }");
    connect(btnLaunchSim, &QPushButton::clicked, this, [this]() { SimulatorLauncher::launch(this); });
    menuBar()->setCornerWidget(btnLaunchSim, Qt::TopRightCorner);
}

void ClientWindow::setupUi() {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Main horizontal split: Sidebar | Content ─────────────────────────────
    QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setHandleWidth(2);
    splitter->setChildrenCollapsible(false);

    // ── Left Sidebar ──────────────────────────────────────────────────────────
    m_sideNav = new QListWidget(splitter);
    m_sideNav->setObjectName("sideNav");
    m_sideNav->setFixedWidth(190);
    m_sideNav->setSpacing(2);
    m_sideNav->setFrameShape(QFrame::NoFrame);

    // Sidebar logo/header
    QListWidgetItem *headerItem = new QListWidgetItem("  🛰  NETA DRX9000");
    headerItem->setFlags(Qt::NoItemFlags);  // not selectable
    headerItem->setFont(QFont("Segoe UI", 10, QFont::Bold));
    headerItem->setForeground(QColor("#34D399"));
    headerItem->setBackground(QColor("#064E3B"));
    headerItem->setSizeHint(QSize(190, 42));
    m_sideNav->addItem(headerItem);

    // Separator
    QListWidgetItem *sepItem = new QListWidgetItem();
    sepItem->setFlags(Qt::NoItemFlags);
    sepItem->setSizeHint(QSize(190, 1));
    sepItem->setBackground(QColor("#1A3D2B"));
    m_sideNav->addItem(sepItem);

    // Navigation items (index 2 = Page 0, index 3 = Page 1, etc.)
    auto addNavItem = [&](const QString &text) {
        QListWidgetItem *item = new QListWidgetItem(text);
        item->setSizeHint(QSize(190, 46));
        m_sideNav->addItem(item);
    };
    addNavItem("  📡  Üretim Testi");
    addNavItem("  📈  Canlı Grafik");
    addNavItem("  🛰️  Uydu Profilleri");
    addNavItem("  📊  Test Geçmişi & Rapor");
    addNavItem("  ⚙️  Mühendis Ayarları");
    addNavItem("  📜  Sistem Logları");

    m_sideNav->setCurrentRow(2);  // default: Üretim Testi

    // ── Right Page Stack ──────────────────────────────────────────────────────
    m_pageStack = new QStackedWidget(splitter);

    setupProductionPage();
    setupGraphPage();
    setupProfilesPage();
    m_historyWidget = new TestHistoryWidget(this);
    connect(m_historyWidget, &TestHistoryWidget::logMessage, this, &ClientWindow::appendLog);
    setupEngineerSettingsPage();
    setupLogsPage();

    m_pageStack->addWidget(m_pageProduction);   // index 0
    m_pageStack->addWidget(m_pageGraph);        // index 1
    m_pageStack->addWidget(m_pageProfiles);     // index 2
    m_pageStack->addWidget(m_historyWidget);    // index 3
    m_pageStack->addWidget(m_pageEngineer);     // index 4
    m_pageStack->addWidget(m_pageLogs);         // index 5

    splitter->addWidget(m_sideNav);
    splitter->addWidget(m_pageStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    rootLayout->addWidget(splitter);

    // Nav ↔ Page stack wiring (offset by 2 for header + separator items)
    connect(m_sideNav, &QListWidget::currentRowChanged, this, &ClientWindow::onNavItemChanged);

    // ── Status Bar ────────────────────────────────────────────────────────────
    m_statusLabel = new QLabel("Hazır", this);
    m_statusLabel->setStyleSheet("color: #A7C4B5; padding-left: 4px;");
    statusBar()->addWidget(m_statusLabel, 1);

    QLabel *lblFooterRight = new QLabel(
        QString("NETA Elektronik A.Ş.  |  Geliştirici: %1  |  v%2")
            .arg(NETA_APP_DEVELOPER, NETA_APP_VERSION), this);
    lblFooterRight->setStyleSheet("color: #4B7A66; padding-right: 6px; font-size: 8.5pt;");
    statusBar()->addPermanentWidget(lblFooterRight);
}

void ClientWindow::setupProductionPage() {
    m_pageProduction = new QWidget(this);
    QHBoxLayout *mainCols = new QHBoxLayout(m_pageProduction);
    mainCols->setContentsMargins(10, 10, 10, 10);
    mainCols->setSpacing(10);

    // =========================================================================
    // LEFT COLUMN: Ethernet Connection Card + Test Launch + 3x3 Live RF Pastel Grid
    // =========================================================================
    QVBoxLayout *leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);

    // 1. Station & CAT6 Ethernet Info Group (NO serial port controls here!)
    QGroupBox *grpStation = new QGroupBox("🏭 İstasyon & Ethernet (CAT6) Bağlantısı", m_pageProduction);
    QVBoxLayout *stVBox = new QVBoxLayout(grpStation);
    stVBox->setSpacing(8);

    // Row 1: NETA white pill logo + Ethernet Target IP + Ping & Sync button
    QHBoxLayout *stHeaderRow = new QHBoxLayout();
    stHeaderRow->setSpacing(8);

    QLabel *lblLogo = new QLabel(grpStation);
    lblLogo->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 3px 10px;");
    QPixmap pix(":/resources/images/logo.png");
    if (!pix.isNull()) {
        lblLogo->setPixmap(pix.scaled(118, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        lblLogo->setText("<b style='color:#064E3B; font-size:13pt;'>NETA</b>");
    }
    stHeaderRow->addWidget(lblLogo);
    stHeaderRow->addStretch();

    QLabel *lblDutIpTitle = new QLabel("🌐 DRX IP:", grpStation);
    lblDutIpTitle->setStyleSheet("color: #34D399; font-weight: bold;");

    m_txtHeaderDutIp = new QLineEdit(AppConfig::instance().stationDrxHost(), grpStation);
    m_txtHeaderDutIp->setPlaceholderText("192.168.0.168 veya 127.0.0.1:8080");
    m_txtHeaderDutIp->setFixedWidth(185);
    m_txtHeaderDutIp->setToolTip("Test edilecek DUT DRX9000 IP adresi (Simülatör için örn: 127.0.0.1:8080)");
    m_txtHeaderDutIp->setStyleSheet("background-color: #0E2219; color: #34D399; border: 1px solid #1A3D2B; border-radius: 4px; padding: 4px 8px; font-weight: bold;");
    auto applyHeaderDutIp = [this]() {
        if (!m_txtHeaderDutIp) return;
        const QString enteredIp = m_txtHeaderDutIp->text().trimmed();
        if (enteredIp.isEmpty()) return;
        AppConfig &cfg = AppConfig::instance();
        if (enteredIp != cfg.stationDrxHost()) {
            cfg.setStationDrxHost(enteredIp);
            cfg.save();
            m_dutWebClient.setHost(cfg.stationDrxHost(), cfg.stationDrxPort());
            if (m_txtDutDrxIp) {
                m_txtDutDrxIp->blockSignals(true);
                m_txtDutDrxIp->setText(enteredIp);
                m_txtDutDrxIp->blockSignals(false);
            }
            setStatusMessage(QString("DUT DRX IP güncellendi: %1").arg(enteredIp));
        }
    };
    connect(m_txtHeaderDutIp, &QLineEdit::editingFinished, this, applyHeaderDutIp);
    connect(m_txtHeaderDutIp, &QLineEdit::returnPressed, this, &ClientWindow::onEthPingSyncClicked);

    m_btnEthPingSync = new QPushButton("🔌 Ping & Bağlan (ETH)", grpStation);
    m_btnEthPingSync->setCursor(Qt::PointingHandCursor);
    m_btnEthPingSync->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnEthPingSync, &QPushButton::clicked, this, &ClientWindow::onEthPingSyncClicked);

    stHeaderRow->addWidget(lblDutIpTitle);
    stHeaderRow->addWidget(m_txtHeaderDutIp);
    stHeaderRow->addWidget(m_btnEthPingSync);
    stVBox->addLayout(stHeaderRow);

    // Row 2: 4 Clean Pastel Badges (Station / Host Server / ETH Ping / Reference Lock)
    QHBoxLayout *badgeRow = new QHBoxLayout();
    badgeRow->setSpacing(6);

    m_lblStationBadge = new QLabel(
        AppConfig::instance().stationId() > 0
            ? QString("🖥️ İstasyon %1").arg(AppConfig::instance().stationId())
            : QString("🖥️ İstasyon (Atanıyor...)"),
        grpStation);
    m_lblStationBadge->setStyleSheet(StyleHelper::getPastelBlueStyle());

    m_lblConnectionStatus = new QLabel("🌐 Host: Aranıyor...", grpStation);
    m_lblConnectionStatus->setStyleSheet(StyleHelper::getPastelAmberStyle());

    m_lblDbBadge = new QLabel("🗄️ DB: Kontrol...", grpStation);
    m_lblDbBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());

    m_lblEthPingBadge = new QLabel("📶 ETH Ping: Test...", grpStation);
    m_lblEthPingBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());

    m_lblLockBanner = new QLabel("⚪ Kilit: Bekleniyor", grpStation);
    m_lblLockBanner->setStyleSheet(StyleHelper::getPastelSlateStyle());

    m_lblRefModeBadge = new QLabel("🟢 Ref: Canlı Anten", grpStation);
    m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());

    badgeRow->addWidget(m_lblStationBadge, 1);
    badgeRow->addWidget(m_lblConnectionStatus, 1);
    badgeRow->addWidget(m_lblDbBadge, 1);
    badgeRow->addWidget(m_lblEthPingBadge, 1);
    badgeRow->addWidget(m_lblLockBanner, 1);
    badgeRow->addWidget(m_lblRefModeBadge, 1);
    stVBox->addLayout(badgeRow);
    updateDbStatusBadge();

    // Row 3: Role + Operator + Web CGI Session State
    QHBoxLayout *opRow = new QHBoxLayout();
    opRow->setSpacing(8);

    m_lblRolePill = new QLabel("ROL: Tekniker", grpStation);
    m_lblRolePill->setStyleSheet("background-color: #059669; color: white; border-radius: 4px; padding: 3px 8px; font-weight: bold;");

    QLabel *lblOpTitle = new QLabel("Operatör:", grpStation);
    m_lblOperatorName = new QLabel("Operatör", grpStation);
    m_lblOperatorName->setStyleSheet("font-weight: bold; color: #34D399; font-size: 10pt;");

    m_lblDeviceStatus = new QLabel("🔴 Web Veri: Bekleniyor", grpStation);
    m_lblDeviceStatus->setStyleSheet(StyleHelper::getPastelRedStyle());

    opRow->addWidget(m_lblRolePill);
    opRow->addWidget(lblOpTitle);
    opRow->addWidget(m_lblOperatorName);
    opRow->addStretch();
    opRow->addWidget(m_lblDeviceStatus);
    stVBox->addLayout(opRow);

    // Row 4: Synced Device Identity Strip (fetched after Ping -> Login -> deviceInfo)
    m_lblDevMeta = new QLabel("🆔 Cihaz ID: --   |   💾 SW: --   |   🌐 MAC: --", grpStation);
    m_lblDevMeta->setStyleSheet("background-color: #112D21; color: #A7C4B5; border: 1px solid #1A3D2B; border-radius: 4px; padding: 4px 8px; font-family: Consolas, monospace; font-size: 8.5pt;");
    stVBox->addWidget(m_lblDevMeta);

    leftCol->addWidget(grpStation);

    // 2. Test Execution & Work Order Group
    QGroupBox *grpLaunch = new QGroupBox("🧪 Anten Testi & İş Emri", m_pageProduction);
    QFormLayout *launchForm = new QFormLayout(grpLaunch);
    launchForm->setSpacing(8);
    launchForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_comboOrders = new QComboBox(grpLaunch);
    connect(m_comboOrders, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ClientWindow::onWorkOrderComboChanged);

    QHBoxLayout *profRow = new QHBoxLayout();
    profRow->setSpacing(6);
    m_comboProfiles = new QComboBox(grpLaunch);
    connect(m_comboProfiles, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ClientWindow::onProfileComboChanged);
    QPushButton *btnRefreshProfiles = new QPushButton("🔄 Yenile", grpLaunch);
    btnRefreshProfiles->setToolTip("Uydu profillerini ve Golden Sample değerlerini veritabanından yenile");
    btnRefreshProfiles->setCursor(Qt::PointingHandCursor);
    btnRefreshProfiles->setMinimumHeight(30);
    btnRefreshProfiles->setStyleSheet(
        "QPushButton { background-color: #065F46; color: #E6F4EE; border: 1px solid #10B981; border-radius: 4px; padding: 4px 10px; font-weight: bold; }"
        "QPushButton:hover { background-color: #10B981; color: #071210; }"
    );
    connect(btnRefreshProfiles, &QPushButton::clicked, this, &ClientWindow::loadWorkOrdersAndProfiles);

    QPushButton *btnManageProfiles = new QPushButton("➕ Ekle / Düzenle", grpLaunch);
    btnManageProfiles->setToolTip("Yeni uydu profili ekle veya frekansları düzenle");
    btnManageProfiles->setCursor(Qt::PointingHandCursor);
    btnManageProfiles->setMinimumHeight(30);
    btnManageProfiles->setStyleSheet(
        "QPushButton { background-color: #112D21; color: #34D399; border: 1px solid #34D399; border-radius: 4px; padding: 4px 10px; font-weight: bold; }"
        "QPushButton:hover { background-color: #34D399; color: #071210; }"
    );
    connect(btnManageProfiles, &QPushButton::clicked, this, [this]() {
        if (m_sideNav) m_sideNav->setCurrentRow(4); // Uydu Profilleri sayfasına geç
    });

    profRow->addWidget(m_comboProfiles, 1);
    profRow->addWidget(btnRefreshProfiles);
    profRow->addWidget(btnManageProfiles);

    m_txtTesterName = new QLineEdit(grpLaunch);
    m_txtTesterName->setPlaceholderText("Testi yapan kişi adını giriniz (Zorunlu - Kullanıcı adı değildir)...");
    m_txtTesterName->setStyleSheet("font-size: 10.5pt; font-weight: bold; padding: 6px 10px; border: 1px solid #10B981;");

    m_txtAntennaSn = new QLineEdit(grpLaunch);
    m_txtAntennaSn->setPlaceholderText("Anten barkodunu okutunuz ve Enter'a basınız...");
    m_txtAntennaSn->setStyleSheet("font-size: 11pt; font-weight: bold; padding: 6px 10px;");
    connect(m_txtAntennaSn, &QLineEdit::returnPressed, this, &ClientWindow::onStartTestClicked);

    launchForm->addRow(new QLabel("İş Emri:", grpLaunch), m_comboOrders);
    launchForm->addRow(new QLabel("Uydu Profili:", grpLaunch), profRow);
    launchForm->addRow(new QLabel("Testi Yapan İsim *:", grpLaunch), m_txtTesterName);
    launchForm->addRow(new QLabel("Barkod / Seri No *:", grpLaunch), m_txtAntennaSn);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);

    m_btnStartTest = new QPushButton("🧪 TESTİ BAŞLAT", grpLaunch);
    m_btnStartTest->setMinimumHeight(38);
    m_btnStartTest->setCursor(Qt::PointingHandCursor);
    m_btnStartTest->setStyleSheet(
        "QPushButton { background-color: #10B981; color: #071210; border: none; border-radius: 5px; font-weight: bold; font-size: 11pt; padding: 8px 16px; }"
        "QPushButton:hover { background-color: #34D399; }"
        "QPushButton:disabled { background-color: #0D2318; color: #3D6652; }"
    );
    connect(m_btnStartTest, &QPushButton::clicked, this, &ClientWindow::onStartTestClicked);

    m_btnAbortTest = new QPushButton("⏹ DURDUR", grpLaunch);
    m_btnAbortTest->setMinimumHeight(38);
    m_btnAbortTest->setCursor(Qt::PointingHandCursor);
    m_btnAbortTest->setEnabled(false);
    m_btnAbortTest->setStyleSheet(
        "QPushButton { background-color: #B91C1C; color: #FFFFFF; border: none; border-radius: 5px; font-weight: bold; font-size: 10pt; padding: 8px 16px; }"
        "QPushButton:hover { background-color: #991B1B; }"
        "QPushButton:disabled { background-color: #0D2318; color: #3D6652; }"
    );
    connect(m_btnAbortTest, &QPushButton::clicked, this, &ClientWindow::onAbortTestClicked);

    btnRow->addWidget(m_btnStartTest, 3);
    btnRow->addWidget(m_btnAbortTest, 1);
    launchForm->addRow(btnRow);

    leftCol->addWidget(grpLaunch);

    // 3. 3x3 Live RF Telemetry Pastel Pills (mirrors DRX DashboardTab)
    QGroupBox *grpTelemetry = new QGroupBox("📡 Canlı RF Sinyal Telemetrisi ve Durum Rozetleri", m_pageProduction);
    QGridLayout *telGrid = new QGridLayout(grpTelemetry);
    telGrid->setSpacing(6);

    m_lblLiveChannel = new QLabel("📺 Kanal: Bekleniyor...", grpTelemetry);
    m_lblLiveFreqPol = new QLabel("📡 Frekans: -- MHz (--)", grpTelemetry);
    m_lblStatusSignal = new QLabel("📡 Sinyal: YOK", grpTelemetry);

    m_lblLiveDutCn = new QLabel("📊 DUT C/N: -- dB", grpTelemetry);
    m_lblLiveDelta = new QLabel("📐 Fark (Δ): -- dB", grpTelemetry);
    m_lblStatusVideo = new QLabel("🎬 Video: YOK", grpTelemetry);

    m_lblLiveRefCn = new QLabel("🎯 REF C/N: -- dB", grpTelemetry);
    m_lblLivePower = new QLabel("📶 POW: -- dBm", grpTelemetry);
    m_lblLiveTolerance = new QLabel(QString("⚖️ Tolerans: ±%1 dB").arg(AppConfig::instance().defaultToleranceDb(), 0, 'f', 2), grpTelemetry);

    resetLiveTelemetryPills();

    telGrid->addWidget(m_lblLiveChannel, 0, 0);
    telGrid->addWidget(m_lblLiveFreqPol, 0, 1);
    telGrid->addWidget(m_lblStatusSignal, 0, 2);

    telGrid->addWidget(m_lblLiveDutCn, 1, 0);
    telGrid->addWidget(m_lblLiveDelta, 1, 1);
    telGrid->addWidget(m_lblStatusVideo, 1, 2);

    telGrid->addWidget(m_lblLiveRefCn, 2, 0);
    telGrid->addWidget(m_lblLivePower, 2, 1);
    telGrid->addWidget(m_lblLiveTolerance, 2, 2);

    leftCol->addWidget(grpTelemetry);
    leftCol->addStretch();

    // =========================================================================
    // RIGHT COLUMN: Overall Verdict Card + Progress + Clean 10-Row Table
    // =========================================================================
    QVBoxLayout *rightCol = new QVBoxLayout();
    rightCol->setSpacing(8);

    QGroupBox *grpOutcome = new QGroupBox("🏁 Test Sonuç Özeti", m_pageProduction);
    QVBoxLayout *outVBox = new QVBoxLayout(grpOutcome);
    outVBox->setSpacing(6);

    m_resultBannerFrame = new QFrame(grpOutcome);
    m_resultBannerFrame->setStyleSheet("background-color: #112D21; border: 1px solid #1A3D2B; border-radius: 6px; padding: 10px;");
    QVBoxLayout *bannerVBox = new QVBoxLayout(m_resultBannerFrame);
    bannerVBox->setSpacing(2);

    m_lblOverallResult = new QLabel("BEKLEMEDE", m_resultBannerFrame);
    m_lblOverallResult->setAlignment(Qt::AlignCenter);
    m_lblOverallResult->setStyleSheet("font-size: 20pt; font-weight: bold; color: #A7C4B5;");

    m_lblOverallDetails = new QLabel("Hazır: Anten barkodunu okutunuz", m_resultBannerFrame);
    m_lblOverallDetails->setAlignment(Qt::AlignCenter);
    m_lblOverallDetails->setStyleSheet("font-size: 9.5pt; color: #E6F4EE;");

    bannerVBox->addWidget(m_lblOverallResult);
    bannerVBox->addWidget(m_lblOverallDetails);
    outVBox->addWidget(m_resultBannerFrame);

    m_progressTest = new QProgressBar(grpOutcome);
    m_progressTest->setRange(0, 10);
    m_progressTest->setValue(0);
    m_progressTest->setFormat("%v / %m Kanal");
    m_progressTest->setFixedHeight(22);
    outVBox->addWidget(m_progressTest);

    rightCol->addWidget(grpOutcome);

    // 10-Channel Transponder Table
    QGroupBox *grpChannels = new QGroupBox("📺 Türksat 10 Kanal Test Matrisi", m_pageProduction);
    QVBoxLayout *chVBox = new QVBoxLayout(grpChannels);

    m_tableChannels = new QTableWidget(grpChannels);
    m_tableChannels->setColumnCount(7);
    m_tableChannels->setHorizontalHeaderLabels({"#", "Kanal / Frekans", "DUT C/N", "REF C/N", "Fark (Δ)", "Kilit", "Durum"});
    m_tableChannels->verticalHeader()->setVisible(false);
    m_tableChannels->setSelectionMode(QAbstractItemView::NoSelection);
    m_tableChannels->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableChannels->setAlternatingRowColors(true);
    m_tableChannels->setShowGrid(true);

    QHeaderView *hdr = m_tableChannels->horizontalHeader();
    hdr->setSectionResizeMode(0, QHeaderView::Fixed);
    m_tableChannels->setColumnWidth(0, 34);
    hdr->setSectionResizeMode(1, QHeaderView::Stretch);
    hdr->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    hdr->setSectionResizeMode(6, QHeaderView::Fixed);
    m_tableChannels->setColumnWidth(6, 96);

    chVBox->addWidget(m_tableChannels);
    rightCol->addWidget(grpChannels, 1);

    mainCols->addLayout(leftCol, 52);
    mainCols->addLayout(rightCol, 48);
}

void ClientWindow::setupGraphPage() {
    m_pageGraph = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageGraph);
    vbox->setContentsMargins(10, 10, 10, 10);

    QGroupBox *grpGraph = new QGroupBox("📈 Canlı Sinyal C/N (DUT vs Referans Golden Sample) Telemetri Grafiği", m_pageGraph);
    QVBoxLayout *plotBox = new QVBoxLayout(grpGraph);

    QHBoxLayout *topActions = new QHBoxLayout();
    QLabel *lblHint = new QLabel("Her kanal örnekleme adımında (250 ms) DUT ve Referans çanak C/N eğrileri canlı çizilir.", grpGraph);
    lblHint->setStyleSheet("color: #A7C4B5;");
    QPushButton *btnClearGraph = new QPushButton("🧹 Grafiği Temizle", grpGraph);
    btnClearGraph->setFixedHeight(28);
    connect(btnClearGraph, &QPushButton::clicked, this, &ClientWindow::onClearGraphClicked);
    topActions->addWidget(lblHint);
    topActions->addStretch();
    topActions->addWidget(btnClearGraph);
    plotBox->addLayout(topActions);

    m_plot = new QCustomPlot(grpGraph);
    plotBox->addWidget(m_plot, 1);

    vbox->addWidget(grpGraph);
}

void ClientWindow::setupProfilesPage() {
    m_profileManager = new ProfileManagerWidget(false, nullptr, nullptr, this);
    connect(m_profileManager, &ProfileManagerWidget::profilesChanged,
            this, &ClientWindow::loadWorkOrdersAndProfiles);
    connect(m_profileManager, &ProfileManagerWidget::logMessage,
            this, &ClientWindow::appendLog);
    m_pageProfiles = m_profileManager;
}

void ClientWindow::setupEngineerSettingsPage() {
    m_pageEngineer = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageEngineer);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(10);

    QGridLayout *cardsGrid = new QGridLayout();
    cardsGrid->setSpacing(10);

    // Card 1: RF Test & Tolerance
    QGroupBox *grpRf = new QGroupBox("⚖️ RF Test & Tolerans Parametreleri", m_pageEngineer);
    QFormLayout *rfForm = new QFormLayout(grpRf);
    rfForm->setSpacing(10);

    m_spinTolerance = new QDoubleSpinBox(grpRf);
    m_spinTolerance->setRange(0.20, 5.00);
    m_spinTolerance->setSingleStep(0.10);
    m_spinTolerance->setDecimals(2);
    m_spinTolerance->setSuffix(" dB");
    m_spinTolerance->setValue(AppConfig::instance().defaultToleranceDb());

    m_spinSampleDuration = new QSpinBox(grpRf);
    m_spinSampleDuration->setRange(1, 30);
    m_spinSampleDuration->setSuffix(" sn");
    m_spinSampleDuration->setValue(AppConfig::instance().sampleDurationSec());

    m_lblProfileSummary = new QLabel("Türksat 42.0°E (10 Transponder)", grpRf);
    m_lblProfileSummary->setStyleSheet("color: #34D399; font-weight: bold;");

    QPushButton *btnSaveRf = new QPushButton("💾 Test Parametrelerini Kaydet", grpRf);
    btnSaveRf->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnSaveRf, &QPushButton::clicked, this, &ClientWindow::onSaveEngineerSettingsClicked);

    rfForm->addRow("İzin Verilen Δ Toleransı:", m_spinTolerance);
    rfForm->addRow("Kanal Örnekleme Süresi:", m_spinSampleDuration);
    rfForm->addRow("Aktif Uydu Profili:", m_lblProfileSummary);
    rfForm->addRow(btnSaveRf);
    cardsGrid->addWidget(grpRf, 0, 0);

    // Card 2: DRX Ethernet Web CGI Settings (Primary CAT6 communication)
    QGroupBox *grpWeb = new QGroupBox("🌐 DRX9000 Ethernet & Web CGI Ayarları", m_pageEngineer);
    QFormLayout *webForm = new QFormLayout(grpWeb);
    webForm->setSpacing(10);

    m_txtDutDrxIp = new QLineEdit(AppConfig::instance().stationDrxHost(), grpWeb);
    m_txtRefDrxIp = new QLineEdit(AppConfig::instance().refDrxHost(), grpWeb);
    m_txtDrxUser = new QLineEdit(AppConfig::instance().drxUsername(), grpWeb);
    m_txtDrxPass = new QLineEdit(AppConfig::instance().drxPassword(), grpWeb);
    m_txtDrxPass->setEchoMode(QLineEdit::Password);

    QPushButton *btnSaveWeb = new QPushButton("🔗 Kaydet & Ping + Veri Testi Başlat", grpWeb);
    btnSaveWeb->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnSaveWeb, &QPushButton::clicked, this, &ClientWindow::onSaveWebSettingsClicked);

    webForm->addRow("İstasyon DUT DRX IP:", m_txtDutDrxIp);
    webForm->addRow("Referans (Golden) DRX IP:", m_txtRefDrxIp);
    webForm->addRow("DRX Web Kullanıcı:", m_txtDrxUser);
    webForm->addRow("DRX Web Şifre:", m_txtDrxPass);
    webForm->addRow(btnSaveWeb);
    cardsGrid->addWidget(grpWeb, 0, 1);

    // Anında QSettings (.ini) otomatik kayıt bağlantıları
    auto autoSaveClientSettings = [this]() {
        AppConfig &cfg = AppConfig::instance();
        cfg.setDefaultToleranceDb(m_spinTolerance->value());
        cfg.setSampleDurationSec(m_spinSampleDuration->value());
        cfg.setStationDrxHost(m_txtDutDrxIp->text().trimmed());
        cfg.setRefDrxHost(m_txtRefDrxIp->text().trimmed());
        cfg.setDrxUsername(m_txtDrxUser->text().trimmed());
        cfg.setDrxPassword(m_txtDrxPass->text());
        if (m_txtDbHost) cfg.setDbHost(m_txtDbHost->text().trimmed());
        if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
        if (m_txtDbName) cfg.setDbName(m_txtDbName->text().trimmed());
        if (m_txtDbUser) cfg.setDbUser(m_txtDbUser->text().trimmed());
        if (m_txtDbPass) cfg.setDbPassword(m_txtDbPass->text());
        cfg.save();

        m_dutWebClient.setHost(cfg.stationDrxHost(), cfg.stationDrxPort());
        m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
        if (m_txtHeaderDutIp) {
            m_txtHeaderDutIp->blockSignals(true);
            m_txtHeaderDutIp->setText(cfg.stationDrxHost());
            m_txtHeaderDutIp->blockSignals(false);
        }
    };
    connect(m_spinTolerance, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, autoSaveClientSettings);
    connect(m_spinSampleDuration, QOverload<int>::of(&QSpinBox::valueChanged), this, autoSaveClientSettings);
    connect(m_txtDutDrxIp, &QLineEdit::editingFinished, this, autoSaveClientSettings);
    connect(m_txtRefDrxIp, &QLineEdit::editingFinished, this, autoSaveClientSettings);
    connect(m_txtDrxUser, &QLineEdit::editingFinished, this, autoSaveClientSettings);
    connect(m_txtDrxPass, &QLineEdit::editingFinished, this, autoSaveClientSettings);

    // Card 3: Central MariaDB Database Settings (2x2 grid bottom-left)
    QGroupBox *grpDb = new QGroupBox("🗄️ Merkezi Veritabanı (MariaDB) Bağlantı Ayarları", m_pageEngineer);
    QFormLayout *dbForm = new QFormLayout(grpDb);
    dbForm->setSpacing(10);

    m_txtDbHost = new QLineEdit(AppConfig::instance().dbHost(), grpDb);
    m_txtDbHost->setPlaceholderText("Örn: 192.168.1.50 (Yönetici PC IP)");
    m_spinDbPort = new QSpinBox(grpDb);
    m_spinDbPort->setRange(1, 65535);
    m_spinDbPort->setValue(AppConfig::instance().dbPort());
    m_txtDbName = new QLineEdit(AppConfig::instance().dbName(), grpDb);
    m_txtDbUser = new QLineEdit(AppConfig::instance().dbUser(), grpDb);
    m_txtDbPass = new QLineEdit(AppConfig::instance().dbPassword(), grpDb);
    m_txtDbPass->setEchoMode(QLineEdit::Password);

    m_lblDbConnStatus = new QLabel(grpDb);
    QPushButton *btnDbConnect = new QPushButton("🔄 Kaydet & Veritabanına Bağlan", grpDb);
    btnDbConnect->setCursor(Qt::PointingHandCursor);
    btnDbConnect->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnDbConnect, &QPushButton::clicked, this, &ClientWindow::onReconnectDbClicked);

    connect(m_txtDbHost, &QLineEdit::editingFinished, this, autoSaveClientSettings);
    connect(m_spinDbPort, QOverload<int>::of(&QSpinBox::valueChanged), this, autoSaveClientSettings);
    connect(m_txtDbName, &QLineEdit::editingFinished, this, autoSaveClientSettings);
    connect(m_txtDbUser, &QLineEdit::editingFinished, this, autoSaveClientSettings);
    connect(m_txtDbPass, &QLineEdit::editingFinished, this, autoSaveClientSettings);

    dbForm->addRow("MariaDB Sunucu IP:", m_txtDbHost);
    dbForm->addRow("MariaDB Portu:", m_spinDbPort);
    dbForm->addRow("Veritabanı Adı:", m_txtDbName);
    dbForm->addRow("Kullanıcı / Şifre:", m_txtDbUser);
    dbForm->addRow("Şifre:", m_txtDbPass);
    dbForm->addRow("Durum:", m_lblDbConnStatus);
    dbForm->addRow(btnDbConnect);
    cardsGrid->addWidget(grpDb, 1, 0);

    // Card 4: Optional Legacy RS232 Fallback (strictly tucked away from Technician screen!)
    QGroupBox *grpSerial = new QGroupBox("🔧 Gelişmiş: RS232 Yedek Seri Port (Opsiyonel)", m_pageEngineer);
    QFormLayout *serForm = new QFormLayout(grpSerial);
    serForm->setSpacing(10);

    QLabel *lblSerNote = new QLabel("Birincil iletişim Ethernet (CAT6) üzerindendir. Sadece acil laboratuvar debug için RS232 bağlayın.", grpSerial);
    lblSerNote->setWordWrap(true);
    lblSerNote->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");

    QHBoxLayout *serCtrlRow = new QHBoxLayout();
    m_comboSerialPorts = new QComboBox(grpSerial);
    QPushButton *btnRefresh = new QPushButton("🔄", grpSerial);
    btnRefresh->setFixedSize(32, 28);
    connect(btnRefresh, &QPushButton::clicked, this, &ClientWindow::onRefreshPortsClicked);
    m_btnConnectSerial = new QPushButton("RS232 Bağlan", grpSerial);
    m_btnConnectSerial->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnConnectSerial, &QPushButton::clicked, this, &ClientWindow::onConnectSerialClicked);

    serCtrlRow->addWidget(m_comboSerialPorts, 1);
    serCtrlRow->addWidget(btnRefresh);
    serCtrlRow->addWidget(m_btnConnectSerial);

    serForm->addRow(lblSerNote);
    serForm->addRow("COM Port Seçimi:", serCtrlRow);
    cardsGrid->addWidget(grpSerial, 1, 1);

    updateDbStatusBadge();
    vbox->addLayout(cardsGrid);
    vbox->addStretch();
}

void ClientWindow::setupLogsPage() {
    m_pageLogs = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageLogs);
    vbox->setContentsMargins(10, 10, 10, 10);

    QGroupBox *grpLogs = new QGroupBox("📜 Gerçek Zamanlı Ethernet Ping, Web CGI ve Ağ Log Konsolu", m_pageLogs);
    QVBoxLayout *logBox = new QVBoxLayout(grpLogs);

    QHBoxLayout *topBar = new QHBoxLayout();
    QLabel *lblLogHint = new QLabel("ICMP Ping testleri, HTTP Web CGI JSON yanıtları ve TCP/UDP ağ olayları anlık kaydedilir.", grpLogs);
    lblLogHint->setStyleSheet("color: #A7C4B5;");
    QPushButton *btnClearLog = new QPushButton("🧹 Konsolu Temizle", grpLogs);
    btnClearLog->setFixedHeight(28);
    connect(btnClearLog, &QPushButton::clicked, this, &ClientWindow::onClearLogClicked);
    topBar->addWidget(lblLogHint);
    topBar->addStretch();
    topBar->addWidget(btnClearLog);
    logBox->addLayout(topBar);

    m_txtLog = new QTextEdit(grpLogs);
    m_txtLog->setReadOnly(true);
    m_txtLog->setStyleSheet("background-color: #040B09; color: #34D399; font-family: Consolas, 'Courier New', monospace; font-size: 9.5pt;");
    logBox->addWidget(m_txtLog, 1);

    vbox->addWidget(grpLogs);
}

void ClientWindow::setupPlot() {
    m_plot->setBackground(QBrush(QColor("#0A1F17")));
    m_plot->xAxis->setBasePen(QPen(QColor("#2D5E45")));
    m_plot->yAxis->setBasePen(QPen(QColor("#2D5E45")));
    m_plot->xAxis->setTickPen(QPen(QColor("#4B7A66")));
    m_plot->yAxis->setTickPen(QPen(QColor("#4B7A66")));
    m_plot->xAxis->setTickLabelColor(QColor("#E6F4EE"));
    m_plot->yAxis->setTickLabelColor(QColor("#E6F4EE"));
    m_plot->xAxis->grid()->setPen(QPen(QColor("#1A3D2B"), 1, Qt::DotLine));
    m_plot->yAxis->grid()->setPen(QPen(QColor("#1A3D2B"), 1, Qt::DotLine));
    m_plot->xAxis->setLabel("Örneklem Sayısı (Zaman)");
    m_plot->yAxis->setLabel("C/N Sinyal Kalitesi (dB)");
    m_plot->xAxis->setLabelColor(QColor("#A7C4B5"));
    m_plot->yAxis->setLabelColor(QColor("#A7C4B5"));

    m_plot->addGraph();
    m_plot->graph(0)->setPen(QPen(QColor("#34D399"), 2.5));
    m_plot->graph(0)->setName("DUT C/N (Test Anteni)");

    m_plot->addGraph();
    m_plot->graph(1)->setPen(QPen(QColor("#FBBF24"), 2.5));
    m_plot->graph(1)->setName("REF C/N (Referans Çanak)");

    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(10, 31, 23, 220)));
    m_plot->legend->setBorderPen(QPen(QColor("#1A3D2B")));
    m_plot->legend->setTextColor(QColor("#E6F4EE"));

    m_plot->xAxis->setRange(0, 30);
    m_plot->yAxis->setRange(0, 20);
    m_plot->replot();
}

void ClientWindow::resetLiveTelemetryPills() {
    m_lblLiveChannel->setText("📺 Kanal: Bekleniyor...");
    m_lblLiveChannel->setStyleSheet(StyleHelper::getPastelSlateStyle());

    m_lblLiveFreqPol->setText("📡 Frekans: -- MHz (-- )");
    m_lblLiveFreqPol->setStyleSheet(StyleHelper::getPastelSlateStyle());

    m_lblStatusSignal->setText("📡 Sinyal: YOK");
    m_lblStatusSignal->setStyleSheet(StyleHelper::getPastelRedStyle());

    m_lblLiveDutCn->setText("📊 DUT C/N: -- dB");
    m_lblLiveDutCn->setStyleSheet(StyleHelper::getPastelBlueStyle());

    m_lblLiveDelta->setText("📐 Fark (Δ): -- dB");
    m_lblLiveDelta->setStyleSheet(StyleHelper::getPastelPurpleStyle());

    m_lblStatusVideo->setText("🎬 Video: YOK");
    m_lblStatusVideo->setStyleSheet(StyleHelper::getPastelRedStyle());

    m_lblLiveRefCn->setText("🎯 REF C/N: -- dB");
    m_lblLiveRefCn->setStyleSheet(StyleHelper::getPastelOrangeStyle());

    m_lblLivePower->setText("📶 POW: -- dBm");
    m_lblLivePower->setStyleSheet(StyleHelper::getPastelGreenStyle());

    m_lblLiveTolerance->setText(QString("⚖️ Tolerans: ±%1 dB").arg(AppConfig::instance().defaultToleranceDb(), 0, 'f', 2));
    m_lblLiveTolerance->setStyleSheet(StyleHelper::getPastelCyanStyle());
}

void ClientWindow::loadWorkOrdersAndProfiles() {
    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        db.connectToDatabase(AppConfig::instance().dbHost(), AppConfig::instance().dbPort(),
                             AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                             AppConfig::instance().dbPassword());
    }
    if (db.isConnected()) {
        db.ensureDefaultDataIfMissing();
    }

    m_workOrders = db.getActiveWorkOrders();
    m_profiles = db.getProfiles();
    AppConfig &cfg = AppConfig::instance();

    if (m_comboOrders) {
        m_comboOrders->blockSignals(true);
        m_comboOrders->clear();
        int targetWoIdx = 0;
        const int savedWoId = cfg.lastWorkOrderId();
        for (int i = 0; i < m_workOrders.size(); ++i) {
            const auto &wo = m_workOrders[i];
            m_comboOrders->addItem(QString("%1 - %2 (%3)").arg(wo.orderNo, wo.customerName, wo.productCode), wo.id);
            if (savedWoId > 0 && wo.id == savedWoId) {
                targetWoIdx = i;
            }
        }
        if (!m_workOrders.isEmpty()) {
            m_comboOrders->setCurrentIndex(targetWoIdx);
        }
        m_comboOrders->blockSignals(false);
    }

    if (m_comboProfiles) {
        m_comboProfiles->blockSignals(true);
        int prevId = (m_currentProfile.id > 0) ? m_currentProfile.id : cfg.lastProfileId();
        m_comboProfiles->clear();
        int targetIdx = 0;
        for (int i = 0; i < m_profiles.size(); ++i) {
            const auto &p = m_profiles[i];
            m_comboProfiles->addItem(QString("%1 (%2, %3 Kanal)")
                                         .arg(p.profileName, p.orbitalPosition)
                                         .arg(p.channels.size()),
                                     p.id);
            if (p.id == prevId) {
                targetIdx = i;
            }
        }
        m_comboProfiles->blockSignals(false);

        if (!m_profiles.isEmpty()) {
            m_comboProfiles->setCurrentIndex(targetIdx);
            onProfileComboChanged(targetIdx);
        }
    } else if (!m_profiles.isEmpty()) {
        m_currentProfile = m_profiles.first();
        populateChannelTable();
    }

    if (m_profileManager) {
        m_profileManager->reloadProfiles(m_currentProfile.id);
    }
}

void ClientWindow::onWorkOrderComboChanged(int index) {
    if (index < 0 || index >= m_workOrders.size() || !m_comboProfiles) return;
    AppConfig::instance().setLastWorkOrderId(m_workOrders[index].id);

    int targetProfId = m_workOrders[index].satelliteProfileId;
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == targetProfId) {
            m_comboProfiles->setCurrentIndex(i);
            break;
        }
    }
}

void ClientWindow::onProfileComboChanged(int index) {
    if (index < 0 || index >= m_profiles.size()) return;
    m_currentProfile = m_profiles[index];
    AppConfig::instance().setLastProfileId(m_currentProfile.id);

    if (m_lblProfileSummary) {
        m_lblProfileSummary->setText(QString("%1 (%2, %3 Kanal)")
                                         .arg(m_currentProfile.profileName, m_currentProfile.orbitalPosition)
                                         .arg(m_currentProfile.channels.size()));
    }
    if (m_lblLiveTolerance) {
        m_lblLiveTolerance->setText(QString("⚖️ Tolerans: ±%1 dB").arg(m_currentProfile.defaultToleranceDb, 0, 'f', 2));
    }
    populateChannelTable();
}

void ClientWindow::onReferenceModeChanged(ReferenceMode mode) {
    AppConfig::instance().setLastReferenceMode(mode);
    if (m_stateMachine) {
        m_stateMachine->setReferenceMode(mode);
    }
    if (m_lblRefModeBadge) {
        if (mode == ReferenceMode::GoldenSample) {
            m_lblRefModeBadge->setText("🌟 Ref: Golden Sample");
            m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());
        } else {
            m_lblRefModeBadge->setText("🟢 Ref: Canlı Anten");
            m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());
        }
    }
    populateChannelTable();
}

void ClientWindow::populateChannelTable() {
    int count = m_currentProfile.channels.isEmpty() ? 10 : m_currentProfile.channels.size();
    m_tableChannels->setRowCount(count);
    m_progressTest->setRange(0, count);
    m_progressTest->setValue(0);

    bool isGoldenMode = (m_stateMachine && m_stateMachine->referenceMode() == ReferenceMode::GoldenSample);

    for (int i = 0; i < count; ++i) {
        m_tableChannels->setRowHeight(i, 36);

        QTableWidgetItem *itIdx = new QTableWidgetItem(QString::number(i + 1));
        itIdx->setTextAlignment(Qt::AlignCenter);

        QString chLabel = QString("Kanal %1").arg(i + 1);
        QString refPreview = "-- dB";
        if (i < m_currentProfile.channels.size()) {
            const auto &ch = m_currentProfile.channels[i];
            chLabel = QString("%1 (%2 MHz %3)").arg(ch.channelName).arg(ch.frequencyMhz).arg(ch.polarization);
            if (isGoldenMode) {
                double gCn = ch.goldenCnDb;
                double gPow = ch.goldenPowDbm;
                double gTol = ch.toleranceDb;
                bool found = DbManager::instance().findGoldenSampleForChannel(
                    ch.frequencyMhz, ch.symbolRate, ch.polarization, gCn, gPow, gTol);
                refPreview = (found || ch.hasGoldenSample)
                                 ? QString("%1 dB (G)").arg(gCn, 0, 'f', 1)
                                 : "Ölçülmedi";
            }
        }

        QTableWidgetItem *itName = new QTableWidgetItem(chLabel);
        QTableWidgetItem *itDut = new QTableWidgetItem("-- dB");
        itDut->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem *itRef = new QTableWidgetItem(refPreview);
        itRef->setTextAlignment(Qt::AlignCenter);
        if (isGoldenMode) {
            itRef->setForeground(QBrush(QColor("#FBBF24")));
        }
        QTableWidgetItem *itDelta = new QTableWidgetItem("--");
        itDelta->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem *itLock = new QTableWidgetItem("S:- V:-");
        itLock->setTextAlignment(Qt::AlignCenter);
        QTableWidgetItem *itStat = new QTableWidgetItem("BEKLİYOR");
        itStat->setTextAlignment(Qt::AlignCenter);
        itStat->setForeground(QBrush(QColor("#94A3B8")));

        m_tableChannels->setItem(i, 0, itIdx);
        m_tableChannels->setItem(i, 1, itName);
        m_tableChannels->setItem(i, 2, itDut);
        m_tableChannels->setItem(i, 3, itRef);
        m_tableChannels->setItem(i, 4, itDelta);
        m_tableChannels->setItem(i, 5, itLock);
        m_tableChannels->setItem(i, 6, itStat);
    }
}

void ClientWindow::onEthPingSyncClicked() {
    if (m_txtHeaderDutIp) {
        const QString enteredIp = m_txtHeaderDutIp->text().trimmed();
        AppConfig &cfg = AppConfig::instance();
        if (!enteredIp.isEmpty() && enteredIp != cfg.stationDrxHost()) {
            cfg.setStationDrxHost(enteredIp);
            cfg.save();
            m_dutWebClient.setHost(cfg.stationDrxHost(), cfg.stationDrxPort());
            if (m_txtDutDrxIp) {
                m_txtDutDrxIp->setText(enteredIp);
            }
        }
    }
    m_lblEthPingBadge->setText("📶 ETH Ping: Aranıyor...");
    m_lblEthPingBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());
    setStatusMessage(QString("Ethernet Ping testi ve veri yenileme çalışıyor (%1)...").arg(m_dutWebClient.host()));
    m_dutWebClient.testConnectionAndSync();
}

void ClientWindow::onEthPingFinished(bool reachable, int latencyMs, const QString &hostIp) {
    if (reachable) {
        m_lblEthPingBadge->setText(QString("📶 Ping: %1 ms (OK)").arg(latencyMs));
        m_lblEthPingBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());

        if (!m_dutWebClient.isLoggedIn()) {
            m_lblDeviceStatus->setText("🟡 Web: Oturum Açılıyor...");
            m_lblDeviceStatus->setStyleSheet(StyleHelper::getPastelAmberStyle());
        }
    } else {
        m_lblEthPingBadge->setText("❌ Ping: Yanıt Yok");
        m_lblEthPingBadge->setStyleSheet(StyleHelper::getPastelRedStyle());

        m_lblDeviceStatus->setText("🔴 DRX ETH: Erişilemiyor");
        m_lblDeviceStatus->setStyleSheet(StyleHelper::getPastelRedStyle());

        if (m_lblDevMeta) {
            m_lblDevMeta->setText(QString("🆔 Cihaz ID: --   |   💾 SW: --   |   🌐 IP: %1 (Yanıt Yok)").arg(hostIp));
        }
    }
}

void ClientWindow::onWebLoginSucceeded(int sessionKey) {
    m_lblDeviceStatus->setText(QString("🟢 Web Veri: Hazır (Key: %1)").arg(sessionKey));
    m_lblDeviceStatus->setStyleSheet(StyleHelper::getPastelGreenStyle());
    setStatusMessage(QString("DRX9000 Ethernet & Web CGI bağlantısı hazır (%1, Key %2)").arg(m_dutWebClient.host()).arg(sessionKey));
}

void ClientWindow::onWebLoginFailed(const QString &reason) {
    m_lblDeviceStatus->setText("🔴 Web Login Hata");
    m_lblDeviceStatus->setStyleSheet(StyleHelper::getPastelRedStyle());
    setStatusMessage("Web CGI oturumu açılamadı: " + reason);
}

void ClientWindow::onWebDeviceInfoReceived(const QString &swVersion, const QString &deviceId, const QString &mac, const QString &/*tunerModel*/) {
    if (m_lblDevMeta) {
        m_lblDevMeta->setText(QString("🆔 Cihaz ID: %1   |   💾 SW: %2   |   🌐 MAC: %3")
                                  .arg(deviceId.isEmpty() ? "--" : deviceId,
                                       swVersion.isEmpty() ? "--" : swVersion,
                                       mac.isEmpty() ? "--" : mac));
    }
}

void ClientWindow::onRefreshPortsClicked() {
    if (!m_comboSerialPorts) return;
    m_comboSerialPorts->clear();
    QStringList ports = SerialWorker::availablePorts();
    m_comboSerialPorts->addItems(ports);

    int idx = m_comboSerialPorts->findText(AppConfig::instance().serialPort());
    if (idx >= 0) m_comboSerialPorts->setCurrentIndex(idx);
}

void ClientWindow::onConnectSerialClicked() {
    if (m_dutSerial.isConnected()) {
        m_dutSerial.closePort();
    } else if (m_comboSerialPorts) {
        QString port = m_comboSerialPorts->currentText();
        if (port.isEmpty()) {
            QMessageBox::warning(this, "Uyarı", "Lütfen bir seri port seçiniz!");
            return;
        }
        AppConfig::instance().setSerialPort(port);
        AppConfig::instance().save();
        m_dutSerial.openPort(port, AppConfig::instance().serialBaudRate());
    }
}

void ClientWindow::onReconnectHostClicked() {
    if (m_txtHostIp && m_spinTcpPort && m_spinUdpPort) {
        AppConfig &cfg = AppConfig::instance();
        cfg.setHostIp(m_txtHostIp->text().trimmed());
        cfg.setTcpPort(static_cast<quint16>(m_spinTcpPort->value()));
        cfg.setUdpBeaconPort(static_cast<quint16>(m_spinUdpPort->value()));
        if (m_txtDbHost) cfg.setDbHost(m_txtDbHost->text().trimmed());
        if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
        if (m_txtDbName) cfg.setDbName(m_txtDbName->text().trimmed());
        if (m_txtDbUser) cfg.setDbUser(m_txtDbUser->text().trimmed());
        if (m_txtDbPass) cfg.setDbPassword(m_txtDbPass->text());
        cfg.save();
    }
    m_netClient.connectToHost(AppConfig::instance().hostIp(), AppConfig::instance().tcpPort());
    appendLog(QString("Yönetici Sunucuya bağlanılıyor: %1:%2").arg(AppConfig::instance().hostIp()).arg(AppConfig::instance().tcpPort()));
}

void ClientWindow::updateDbStatusBadge() {
    const AppConfig &cfg = AppConfig::instance();
    const bool ok = DbManager::instance().isConnected();
    if (m_lblDbBadge) {
        if (ok) {
            m_lblDbBadge->setText(QString("🟢 DB: %1:%2").arg(cfg.dbHost()).arg(cfg.dbPort()));
            m_lblDbBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());
        } else {
            m_lblDbBadge->setText("🔴 DB: Bağlı Değil");
            m_lblDbBadge->setStyleSheet(StyleHelper::getPastelRedStyle());
        }
    }
    if (m_lblDbConnStatus) {
        if (ok) {
            m_lblDbConnStatus->setText(QString("🟢 Bağlı (%1:%2 / %3)")
                                           .arg(cfg.dbHost()).arg(cfg.dbPort())
                                           .arg(DbManager::instance().activeDriver()));
            m_lblDbConnStatus->setStyleSheet("color: #34D399; font-weight: bold;");
        } else {
            m_lblDbConnStatus->setText(QString("🔴 Bağlı Değil (%1:%2)")
                                           .arg(cfg.dbHost()).arg(cfg.dbPort()));
            m_lblDbConnStatus->setStyleSheet("color: #F87171; font-weight: bold;");
        }
    }
}

void ClientWindow::onReconnectDbClicked() {
    AppConfig &cfg = AppConfig::instance();
    if (m_txtDbHost) cfg.setDbHost(m_txtDbHost->text().trimmed());
    if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
    if (m_txtDbName) cfg.setDbName(m_txtDbName->text().trimmed());
    if (m_txtDbUser) cfg.setDbUser(m_txtDbUser->text().trimmed());
    if (m_txtDbPass) cfg.setDbPassword(m_txtDbPass->text());
    cfg.save();

    bool ok = DbManager::instance().connectToDatabase(
        cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword());

    if (ok) {
        DbManager::instance().ensureDefaultDataIfMissing();
        loadWorkOrdersAndProfiles();
        if (m_profileManager) m_profileManager->reloadProfiles();
        updateDbStatusBadge();
        appendLog(QString("[Veritabanı] Bağlantı başarılı: %1:%2 (%3)").arg(cfg.dbHost()).arg(cfg.dbPort()).arg(cfg.dbName()));
        QMessageBox::information(this, "Veritabanı Bağlandı",
                                 QString("Merkezi veritabanına başarıyla bağlanıldı:\n%1:%2 (%3)")
                                     .arg(cfg.dbHost()).arg(cfg.dbPort()).arg(cfg.dbName()));
    } else {
        updateDbStatusBadge();
        appendLog(QString("[Veritabanı HATA] %1").arg(DbManager::instance().lastError()));
        QMessageBox::warning(this, "Veritabanı Bağlantı Hatası",
                             QString("Veritabanına bağlanılamadı (%1:%2):\n%3")
                                 .arg(cfg.dbHost()).arg(cfg.dbPort())
                                 .arg(DbManager::instance().lastError()));
    }
}

void ClientWindow::onHostDiscovered(const QString &hostIp, const QString &serverName, quint16 tcpPort) {
    if (!m_netClient.isConnected()) {
        appendLog(QString("Lobi keşfedildi: %1 (%2:%3). Bağlanılıyor...").arg(serverName, hostIp).arg(tcpPort));
        AppConfig::instance().setHostIp(hostIp);
        AppConfig::instance().setDbHost(hostIp);
        AppConfig::instance().setTcpPort(tcpPort);
        AppConfig::instance().save();
        if (m_txtHostIp) m_txtHostIp->setText(hostIp);
        if (m_txtDbHost) m_txtDbHost->setText(hostIp);
        if (!DbManager::instance().isConnected()) {
            if (DbManager::instance().connectToDatabase(
                    hostIp, AppConfig::instance().dbPort(),
                    AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                    AppConfig::instance().dbPassword())) {
                DbManager::instance().ensureDefaultDataIfMissing();
                loadWorkOrdersAndProfiles();
                if (m_profileManager) m_profileManager->reloadProfiles();
                updateDbStatusBadge();
            }
        }
        m_netClient.connectToHost(hostIp, tcpPort);
    }
}

void ClientWindow::onTcpConnected() {
    m_lblConnectionStatus->setText(QString("🌐 Host: Bağlı (%1)").arg(AppConfig::instance().hostIp()));
    m_lblConnectionStatus->setStyleSheet(StyleHelper::getPastelGreenStyle());

    m_lblLockBanner->setText("🔓 Referans Müsait");
    m_lblLockBanner->setStyleSheet(StyleHelper::getPastelGreenStyle());

    setStatusMessage(QString("Yönetici Sunucuya Bağlı (%1)").arg(AppConfig::instance().hostIp()));

    if (!DbManager::instance().isConnected() || m_workOrders.isEmpty()) {
        AppConfig::instance().setDbHost(AppConfig::instance().hostIp());
        loadWorkOrdersAndProfiles();
    }

    m_netClient.registerStation(AppConfig::instance().stationId(), AppConfig::instance().stationName());
}

void ClientWindow::onTcpDisconnected() {
    m_lblConnectionStatus->setText("🔴 Host: Bağlantı Yok");
    m_lblConnectionStatus->setStyleSheet(StyleHelper::getPastelRedStyle());

    m_lblLockBanner->setText("⚪ Kilit: Bekleniyor");
    m_lblLockBanner->setStyleSheet(StyleHelper::getPastelSlateStyle());

    setStatusMessage("Yönetici Sunucu bağlantısı yok. Lobi araması devam ediyor...");
}

void ClientWindow::onStartTestClicked() {
    QString testerName = m_txtTesterName ? m_txtTesterName->text().trimmed() : QString();
    if (testerName.isEmpty()) {
        QMessageBox::warning(this, "Zorunlu İsim Eksik",
                             "Her test için 'Testi Yapan İsim' alanını doldurmak zorunludur!\n\n"
                             "• Bu alan giriş kullanıcı adından bağımsızdır.\n"
                             "• İsim girilmeden hiçbir koşulda test başlatılamaz.");
        if (m_txtTesterName) m_txtTesterName->setFocus();
        return;
    }

    QString sn = m_txtAntennaSn->text().trimmed();
    if (sn.isEmpty()) {
        QMessageBox::warning(this, "Uyarı", "Lütfen anten barkodunu okutunuz!");
        m_txtAntennaSn->setFocus();
        return;
    }

#if defined(NETA_ALGORITMA_1_FOUR_PC)
    if (!m_netClient.isConnected()) {
        QMessageBox::warning(this, "Yönetici Sunucu Bağlantısı Yok",
                             "Yönetici Sunucuya (Host PC) henüz bağlı değilsiniz!\n\n"
                             "• Çoklu istasyon fabrika testlerinde referans çanak anten senkronizasyonu ve kilit mekanizması için Yönetici programının açık olması gereklidir.\n"
                             "• Lütfen Yönetici Sunucu bilgisayarındaki uygulamayı başlatınız.");
        return;
    }
#endif

    // Ethernet-first hardware validation (Ping verified or optional RS232 fallback ready)
    bool ethReady = m_dutWebClient.isReachable();
    bool serialFallbackReady = m_dutSerial.isConnected() && m_dutSerial.isDeviceReady();
    if (!ethReady && !serialFallbackReady) {
        onEthPingSyncClicked();
        QMessageBox::warning(this, "DRX Ethernet Bağlantısı Yok",
                             QString("Test cihazı DRX9000 (%1) ile CAT6 Ethernet bağlantısı doğrulanamadı (Ping yanıtı yok)!\n\n"
                                     "• Lütfen CAT6 kablosunu ve cihazın güç durumunu kontrol edin.\n"
                                     "• Şu anda otomatik yeniden Ping & Veri Yenileme testi başlatıldı.")
                                 .arg(m_dutWebClient.host()));
        return;
    }

    // Sync active reference mode from Host (Alg 1) or DB (Alg 2)
    ReferenceMode activeRefMode = m_netClient.isConnected()
                                      ? m_netClient.referenceMode()
                                      : DbManager::instance().getReferenceMode();
    onReferenceModeChanged(activeRefMode);

    if (activeRefMode == ReferenceMode::GoldenSample) {
        bool anyGolden = false;
        for (const auto &ch : m_currentProfile.channels) {
            double gCn = 0.0, gPow = 0.0, gTol = 1.5;
            if (ch.hasGoldenSample || DbManager::instance().findGoldenSampleForChannel(
                    ch.frequencyMhz, ch.symbolRate, ch.polarization, gCn, gPow, gTol)) {
                anyGolden = true;
                break;
            }
        }
        if (!anyGolden) {
            auto ans = QMessageBox::question(
                this,
                "Golden Sample Kaydı Eksik",
                QString("Yönetici şu anda 'Kayıtlı Golden Sample' modunu aktif etmiş durumda ancak '%1' profilinde "
                        "henüz ölçülmüş/kayıtlı bir Golden Sample değeri bulunmuyor (0.0 dB).\n\n"
                        "Yine de teste devam edilsin mi?")
                    .arg(m_currentProfile.profileName),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (ans != QMessageBox::Yes) {
                return;
            }
        }
    }

    int woId = m_comboOrders->currentData().toInt();
    m_activeAntennaSn = sn;

    populateChannelTable();
    resetLiveTelemetryPills();
    onClearGraphClicked();

    m_resultBannerFrame->setStyleSheet("background-color: #112D21; border: 2px solid #10B981; border-radius: 6px; padding: 10px;");
    m_lblOverallResult->setText("TEST DEVAM EDİYOR");
    m_lblOverallResult->setStyleSheet("font-size: 20pt; font-weight: bold; color: #34D399;");
    m_lblOverallDetails->setText("Referans kilidi talep ediliyor...");

    m_btnStartTest->setEnabled(false);
    m_btnAbortTest->setEnabled(true);
    setStatusMessage(QString("Test başlatıldı (CAT6 Ethernet): Seri No %1").arg(sn));

    if (!m_stateMachine->startTest(sn, m_currentProfile, woId, 0, testerName)) {
        m_btnStartTest->setEnabled(true);
        m_btnAbortTest->setEnabled(false);
        m_resultBannerFrame->setStyleSheet("background-color: #112D21; border: 1px solid #1A3D2B; border-radius: 6px; padding: 10px;");
        m_lblOverallResult->setText("BEKLEMEDE");
        m_lblOverallResult->setStyleSheet("font-size: 20pt; font-weight: bold; color: #A7C4B5;");
        m_lblOverallDetails->setText("Hazır: Uydu profili veya kanal listesi yüklenemedi.");
        QMessageBox::warning(this, "Test Başlatılamadı",
                             "Aktif uydu transponder profili bulunamadı veya test zaten devam ediyor.\n"
                             "Lütfen veritabanı bağlantısını ve uydu profillerini kontrol ediniz.");
    }
}

void ClientWindow::onAbortTestClicked() {
    m_stateMachine->abortTest("Operatör tarafından durduruldu.");
}

void ClientWindow::onStateChanged(TestState /*state*/, const QString &stateName) {
    m_lblLockBanner->setText("🔒 " + stateName);
    m_lblOverallDetails->setText("Aşama: " + stateName);
    setStatusMessage("Test Durumu: " + stateName);
    if (m_netClient.isConnected()) {
        m_netClient.sendHeartbeat(stateName, m_stateMachine->currentChannelIndex() + 1, m_activeAntennaSn);
    }
}

void ClientWindow::onChannelStarted(int channelNo, const QString &name, int freq, const QString &pol) {
    m_lblLiveChannel->setText(QString("📺 CH%1: %2").arg(channelNo).arg(name));
    m_lblLiveChannel->setStyleSheet(StyleHelper::getPastelBlueStyle());

    m_lblLiveFreqPol->setText(QString("📡 %1 MHz (%2)").arg(freq).arg(pol));
    m_lblLiveFreqPol->setStyleSheet(StyleHelper::getPastelCyanStyle());

    if (m_netClient.isConnected()) {
        m_netClient.sendHeartbeat("TESTING", channelNo, m_activeAntennaSn);
    }

    int row = channelNo - 1;
    if (row >= 0 && row < m_tableChannels->rowCount()) {
        for (int c = 0; c < m_tableChannels->columnCount(); ++c) {
            if (auto *item = m_tableChannels->item(row, c)) {
                item->setBackground(QBrush(QColor("#164832")));
            }
        }
        if (auto *itStat = m_tableChannels->item(row, 6)) {
            itStat->setText("TESTTE...");
            itStat->setForeground(QBrush(QColor("#34D399")));
        }
        m_tableChannels->scrollToItem(m_tableChannels->item(row, 0));
    }
}

void ClientWindow::onChannelSampleUpdated(int channelNo, double dutCn, double dutPow, double refCn, double /*refPow*/, double delta) {
    m_lblLiveDutCn->setText(QString("📊 DUT C/N: %1 dB").arg(dutCn, 0, 'f', 1));
    m_lblLiveRefCn->setText(QString("🎯 REF C/N: %1 dB").arg(refCn, 0, 'f', 1));
    m_lblLivePower->setText(QString("📶 POW: %1 dBm").arg(dutPow, 0, 'f', 1));

    QString sign = delta > 0 ? "+" : "";
    m_lblLiveDelta->setText(QString("📐 Fark (Δ): %1%2 dB").arg(sign).arg(delta, 0, 'f', 2));

    bool tolOk = qAbs(delta) <= m_currentProfile.defaultToleranceDb;
    m_lblLiveDelta->setStyleSheet(tolOk ? StyleHelper::getPastelPurpleStyle() : StyleHelper::getPastelRedStyle());

    int row = channelNo - 1;
    if (row >= 0 && row < m_tableChannels->rowCount()) {
        if (auto *itDut = m_tableChannels->item(row, 2)) itDut->setText(QString("%1 dB").arg(dutCn, 0, 'f', 1));
        if (auto *itRef = m_tableChannels->item(row, 3)) itRef->setText(QString("%1 dB").arg(refCn, 0, 'f', 1));
        if (auto *itDelta = m_tableChannels->item(row, 4)) itDelta->setText(QString("%1%2 dB").arg(sign).arg(delta, 0, 'f', 2));
    }

    int count = m_plot->graph(0)->dataCount();
    m_plot->graph(0)->addData(count, dutCn);
    m_plot->graph(1)->addData(count, refCn);
    m_plot->xAxis->setRange(qMax(0, count - 30), count + 5);
    m_plot->replot();
}

void ClientWindow::onChannelCompleted(const ChannelSampleResult &result) {
    m_progressTest->setValue(result.channelNo);

    int row = result.channelNo - 1;
    if (row >= 0 && row < m_tableChannels->rowCount()) {
        QString sign = result.deltaDb > 0 ? "+" : "";
        QString lockStr = QString("%1 %2")
                              .arg(result.carrierLocked ? "S:+" : "S:-")
                              .arg(result.videoLocked ? "V:+" : "V:-");

        m_tableChannels->item(row, 2)->setText(QString("%1 dB").arg(result.dutCn, 0, 'f', 1));
        m_tableChannels->item(row, 3)->setText(QString("%1 dB").arg(result.refCn, 0, 'f', 1));
        m_tableChannels->item(row, 4)->setText(QString("%1%2 dB").arg(sign).arg(result.deltaDb, 0, 'f', 2));
        m_tableChannels->item(row, 5)->setText(lockStr);

        QColor rowBg("#0A1F17");
        QColor statFg("#A7C4B5");
        QString statText = result.status;

        if (result.status == "PASS") {
            rowBg = QColor("#063829");
            statFg = QColor("#34D399");
            statText = "✔ GEÇTİ";
        } else if (result.status == "WARNING") {
            rowBg = QColor("#3B2809");
            statFg = QColor("#FBBF24");
            statText = "⚠ UYARI";
        } else {
            rowBg = QColor("#3F1219");
            statFg = QColor("#F87171");
            statText = "✖ KALDI";
        }

        for (int c = 0; c < m_tableChannels->columnCount(); ++c) {
            if (auto *it = m_tableChannels->item(row, c)) {
                it->setBackground(QBrush(rowBg));
            }
        }
        auto *itStat = m_tableChannels->item(row, 6);
        itStat->setText(statText);
        itStat->setForeground(QBrush(statFg));
        QFont f = itStat->font();
        f.setBold(true);
        itStat->setFont(f);
    }
}

void ClientWindow::onWaitingForLockUpdate(int queuePos, int activeStation) {
    m_lblLockBanner->setText(QString("⏳ Sıra: %1 (Masa %2)").arg(queuePos).arg(activeStation));
    m_lblLockBanner->setStyleSheet(StyleHelper::getPastelAmberStyle());
}

void ClientWindow::onTestFinished(const TestRunData &finalResult) {
    m_btnStartTest->setEnabled(true);
    m_btnAbortTest->setEnabled(false);

    if (finalResult.overallResult == "PASS") {
        m_resultBannerFrame->setStyleSheet("background-color: #064E3B; border: 2px solid #10B981; border-radius: 6px; padding: 10px;");
        m_lblOverallResult->setText("✔ GEÇTİ (PASS)");
        m_lblOverallResult->setStyleSheet("font-size: 22pt; font-weight: bold; color: #34D399;");
        m_lblOverallDetails->setText(QString("Süre: %1 sn  |  Ort. Fark: %2 dB  |  Max Fark: %3 dB")
                                         .arg(finalResult.durationSec)
                                         .arg(finalResult.avgDeltaDb, 0, 'f', 2)
                                         .arg(finalResult.maxDeltaDb, 0, 'f', 2));
        setStatusMessage(QString("Test Tamamlandı [PASS]: %1 (%2 sn)").arg(finalResult.antennaSn).arg(finalResult.durationSec));
    } else if (finalResult.overallResult == "ABORTED") {
        m_resultBannerFrame->setStyleSheet("background-color: #451A03; border: 2px solid #F59E0B; border-radius: 6px; padding: 10px;");
        m_lblOverallResult->setText("⏹ İPTAL EDİLDİ");
        m_lblOverallResult->setStyleSheet("font-size: 20pt; font-weight: bold; color: #FBBF24;");
        m_lblOverallDetails->setText(finalResult.failReason);
        setStatusMessage("Test operatör tarafından durduruldu.");
    } else {
        m_resultBannerFrame->setStyleSheet("background-color: #450A0A; border: 2px solid #EF4444; border-radius: 6px; padding: 10px;");
        m_lblOverallResult->setText("✖ KALDI (FAIL)");
        m_lblOverallResult->setStyleSheet("font-size: 22pt; font-weight: bold; color: #F87171;");
        m_lblOverallDetails->setText(QString("Neden: %1").arg(finalResult.failReason));
        setStatusMessage(QString("Test Tamamlandı [FAIL]: %1").arg(finalResult.antennaSn));
    }

    if (m_netClient.isConnected()) {
        m_lblLockBanner->setText("🔓 Referans Müsait");
        m_lblLockBanner->setStyleSheet(StyleHelper::getPastelGreenStyle());
        m_netClient.sendHeartbeat("IDLE", 0, "");
    }
    m_activeAntennaSn.clear();

    DbManager::instance().insertTestRun(finalResult);

    if (m_txtTesterName) m_txtTesterName->clear();
    m_txtAntennaSn->clear();
    if (m_txtTesterName) {
        m_txtTesterName->setFocus();
    } else {
        m_txtAntennaSn->setFocus();
    }
}

void ClientWindow::onSaveEngineerSettingsClicked() {
    double tol = m_spinTolerance->value();
    int sampleSec = m_spinSampleDuration->value();

    AppConfig &cfg = AppConfig::instance();
    cfg.setDefaultToleranceDb(tol);
    cfg.setSampleDurationSec(sampleSec);
    cfg.save();

    m_currentProfile.defaultToleranceDb = tol;
    for (auto &ch : m_currentProfile.channels) {
        ch.toleranceDb = tol;
    }

    m_lblLiveTolerance->setText(QString("⚖️ Tolerans: ±%1 dB").arg(tol, 0, 'f', 2));
    appendLog(QString("[Mühendis] Tolerans: ±%1 dB | Örnekleme: %2 sn olarak kaydedildi.").arg(tol, 0, 'f', 2).arg(sampleSec));
    setStatusMessage("Test ve tolerans parametreleri kaydedildi.");
    QMessageBox::information(this, "Kaydedildi", "RF test ve tolerans parametreleri güncellendi.");
}

void ClientWindow::onSaveWebSettingsClicked() {
    AppConfig &cfg = AppConfig::instance();
    cfg.setStationDrxHost(m_txtDutDrxIp->text().trimmed());
    cfg.setRefDrxHost(m_txtRefDrxIp->text().trimmed());
    cfg.setDrxUsername(m_txtDrxUser->text().trimmed());
    cfg.setDrxPassword(m_txtDrxPass->text());
    cfg.save();

    m_dutWebClient.setHost(cfg.stationDrxHost(), cfg.stationDrxPort());
    m_dutWebClient.setCredentials(cfg.drxUsername(), cfg.drxPassword());
    m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
    m_refWebClient.setCredentials(cfg.drxUsername(), cfg.drxPassword());

    if (m_txtHeaderDutIp) {
        m_txtHeaderDutIp->setText(cfg.stationDrxHost());
    }

    appendLog(QString("[Mühendis] Web CGI adresleri güncellendi -> DUT: %1 | REF: %2. Ping testi başlatılıyor...")
                  .arg(cfg.stationDrxHost(), cfg.refDrxHost()));
    onEthPingSyncClicked();
    QMessageBox::information(this, "Güncellendi", "DRX Ethernet / Web CGI ayarları kaydedildi ve Ping testi başlatıldı.");
}

void ClientWindow::onClearGraphClicked() {
    if (!m_plot) return;
    m_plot->graph(0)->data()->clear();
    m_plot->graph(1)->data()->clear();
    m_plot->xAxis->setRange(0, 30);
    m_plot->replot();
}

void ClientWindow::onClearLogClicked() {
    if (m_txtLog) m_txtLog->clear();
}

void ClientWindow::appendLog(const QString &text) {
    AppLogger::instance().logEvent("STATION", text);
    if (!m_txtLog) return;
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_txtLog->append(QString("[%1] %2").arg(timeStr, text));
}

void ClientWindow::setStatusMessage(const QString &msg) {
    if (m_statusLabel) {
        m_statusLabel->setText("✔ " + msg);
    }
}

void ClientWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}

/**
 * @brief Switches the stacked page view when a sidebar navigation row is selected.
 * @param row Selected row index inside m_sideNav (rows 0..1 are header & separator).
 */
void ClientWindow::onNavItemChanged(int row) {
    if (!m_pageStack) return;
    int pageIdx = row - 2; // rows 0 and 1 are header & separator
    if (pageIdx >= 0 && pageIdx < m_pageStack->count()) {
        m_pageStack->setCurrentIndex(pageIdx);
        if (m_pageStack->widget(pageIdx) == m_historyWidget && m_historyWidget) {
            m_historyWidget->refreshHistory();
        }
    }
}

void ClientWindow::applyUserRoleUi() {
    AppConfig &cfg = AppConfig::instance();
    bool isEng = cfg.isEngineer();

    if (m_lblStationBadge) {
        m_lblStationBadge->setText(
            cfg.stationId() > 0
                ? QString("🖥️ İstasyon %1").arg(cfg.stationId())
                : QString("🖥️ İstasyon (Atanıyor...)"));
    }
    if (m_lblOperatorName) {
        m_lblOperatorName->setText(cfg.currentUserFullName());
    }

    if (isEng) {
        m_lblRolePill->setText("ROL: Mühendis");
        m_lblRolePill->setStyleSheet("background-color: #059669; color: white; border-radius: 4px; padding: 3px 8px; font-weight: bold;");
    } else {
        m_lblRolePill->setText("ROL: Tekniker");
        m_lblRolePill->setStyleSheet("background-color: #065F46; color: #34D399; border: 1px solid #10B981; border-radius: 4px; padding: 3px 8px; font-weight: bold;");
    }

    // Sidebar items:
    // row 0: header, row 1: separator
    // row 2: Üretim Testi (page 0)
    // row 3: Canlı Grafik (page 1)
    // row 4: Uydu Profilleri (page 2 - Station & Engineer access)
    // row 5: Test Geçmişi & Rapor (page 3 - Station & Engineer access)
    // row 6: Mühendis Ayarları (page 4 - Engineer only)
    // row 7: Sistem Logları (page 5 - Engineer only)
    if (m_sideNav && m_pageStack) {
        if (QListWidgetItem *engItem = m_sideNav->item(6)) {
            engItem->setHidden(!isEng);
        }
        if (QListWidgetItem *logItem = m_sideNav->item(7)) {
            logItem->setHidden(!isEng);
        }
        if (!isEng && m_pageStack->currentIndex() >= 4) {
            m_sideNav->setCurrentRow(2);
            m_pageStack->setCurrentIndex(0);
        }
    }
}

void ClientWindow::onStationIdAssigned(int stationId) {
    AppConfig &cfg = AppConfig::instance();
    cfg.setStationId(stationId);
    cfg.save();

#if defined(NETA_ALGORITMA_1_FOUR_PC)
    QString algoTitle = "[4 PC - Kilitli Kuyruk]";
#elif defined(NETA_ALGORITMA_2_THREE_PC)
    QString algoTitle = "[3 PC - DB Kuyruk]";
#else
    QString algoTitle = "";
#endif

    setWindowTitle(QString("%1 - İstasyon %2 (%3) %4 v%5")
                       .arg(NETA_APP_TITLE)
                       .arg(stationId)
                       .arg(cfg.stationName())
                       .arg(algoTitle)
                       .arg(NETA_APP_VERSION));
    if (m_lblStationBadge) {
        m_lblStationBadge->setText(QString("🖥️ İstasyon %1").arg(stationId));
    }
    setStatusMessage(QString("Sunucu tarafından İstasyon #%1 olarak atandı (%2)").arg(stationId).arg(cfg.hostIp()));
}

void ClientWindow::onLogoutClicked() {
    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        applyUserRoleUi();
        setStatusMessage(QString("Oturum güncellendi: %1").arg(AppConfig::instance().currentUserFullName()));
    }
}

void ClientWindow::onReturnLobbyClicked() {
    LobbyWindow *lobbyWin = new LobbyWindow();
    lobbyWin->show();
    this->close();
}
