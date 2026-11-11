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
#include "database/db_manager.h"
#include "core/dependency_installer.h"
#include "ui/common/style_helper.h"
#include "ui/common/about_dialog.h"
#include "ui/common/login_dialog.h"
#include "ui/client/client_window.h"
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

    // Start TCP Server & UDP Beacon
    m_tcpServer.start(AppConfig::instance().tcpPort());
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
    if (m_profileManager) {
        m_profileManager->setActiveReferenceMode(activeMode);
        m_profileManager->reloadProfiles();
    }
    onRefreshHistoryClicked();

    // Immediate startup sequence: ICMP Ping -> Web CGI Login -> DeviceInfo & Status Pull
    QTimer::singleShot(250, this, &HostWindow::onRefEthPingSyncClicked);
}

HostWindow::~HostWindow() {
    AppConfig::instance().save();
    m_refEthPingTimer.stop();
    m_beaconBroadcaster.stop();
    m_tcpServer.stop();
    m_refSerial.closePort();
}

void HostWindow::setupMenuBar() {
    QMenu *menuOturum = menuBar()->addMenu("Oturum");
    QAction *actLogout = menuOturum->addAction("Kullanıcı Değiştir / Mod Seç");
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

    m_lblRefIpBadge = new QLabel(QString("🌐 Ref IP: %1").arg(AppConfig::instance().refDrxHost()), grpHeader);
    m_lblRefIpBadge->setStyleSheet("background-color: #112D21; color: #34D399; border: 1px solid #1A3D2B; border-radius: 4px; padding: 5px 10px; font-weight: bold;");

    m_btnRefEthPingSync = new QPushButton("🔌 Ref Ping & Bağlan (ETH)", grpHeader);
    m_btnRefEthPingSync->setCursor(Qt::PointingHandCursor);
    m_btnRefEthPingSync->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnRefEthPingSync, &QPushButton::clicked, this, &HostWindow::onRefEthPingSyncClicked);

    topLayout->addWidget(lblLogo);
    topLayout->addWidget(m_lblServerStatus);
    topLayout->addWidget(m_lblRefModeBadge);
    topLayout->addWidget(m_lblRefPingBadge);
    topLayout->addWidget(m_lblRefTelemetry);
    topLayout->addWidget(lblRoleBadge);
    topLayout->addWidget(m_lblOperatorName);
    topLayout->addStretch();
    topLayout->addWidget(m_lblRefIpBadge);
    topLayout->addWidget(m_btnRefEthPingSync);
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
    addNavItem("  🛰️  Profiller & Golden");
    addNavItem("  📊  Test Geçmişi & Rapor");
    addNavItem("  📦  İş Emirleri");
    addNavItem("  ⚙️  Sistem Ayarları");
    addNavItem("  📜  Sistem Logları");

    m_sideNav->setCurrentRow(2);

    m_pageStack = new QStackedWidget(splitter);
    setupLobbyPage();
    setupProfilesPage();
    setupHistoryPage();
    setupOrdersPage();
    setupSettingsPage();
    setupLogsPage();

    m_pageStack->addWidget(m_pageLobby);     // index 0
    m_pageStack->addWidget(m_pageProfiles);  // index 1
    m_pageStack->addWidget(m_pageHistory);   // index 2
    m_pageStack->addWidget(m_pageOrders);    // index 3
    m_pageStack->addWidget(m_pageSettings);  // index 4
    m_pageStack->addWidget(m_pageLogs);      // index 5

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

    QGroupBox *grpStations = new QGroupBox("🖥️ Üretim Hattındaki Test Masaları (İstasyon 1 - 3)", m_pageLobby);
    QHBoxLayout *stLayout = new QHBoxLayout(grpStations);
    stLayout->setSpacing(12);

    for (int stId = 1; stId <= 3; ++stId) {
        QFrame *card = new QFrame(grpStations);
        card->setStyleSheet("background-color: #112D21; border: 1px solid #1A3D2B; border-radius: 6px; padding: 12px;");
        QVBoxLayout *cardVBox = new QVBoxLayout(card);
        cardVBox->setSpacing(8);

        QLabel *lblTitle = new QLabel(QString("🖥️ İSTASYON %1").arg(stId), card);
        lblTitle->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");

        StationCardWidgets widgets;
        widgets.lblState = new QLabel("DURUM: BAĞLI DEĞİL", card);
        widgets.lblState->setStyleSheet(StyleHelper::getPendingBadgeStyle());

        widgets.lblIp = new QLabel("IP Adresi: --", card);
        widgets.lblIp->setStyleSheet("color: #A7C4B5;");

        widgets.lblSn = new QLabel("Anten Barkodu: --", card);
        widgets.lblSn->setStyleSheet("font-weight: bold; color: #E6F4EE;");

        widgets.lblChannel = new QLabel("Aktif Kanal: --", card);
        widgets.lblChannel->setStyleSheet("color: #E6F4EE;");

        cardVBox->addWidget(lblTitle);
        cardVBox->addWidget(widgets.lblState);
        cardVBox->addWidget(widgets.lblIp);
        cardVBox->addWidget(widgets.lblSn);
        cardVBox->addWidget(widgets.lblChannel);
        cardVBox->addStretch();

        m_stationWidgets[stId] = widgets;
        stLayout->addWidget(card);
    }
    vbox->addWidget(grpStations);
    vbox->addStretch();
}

void HostWindow::setupProfilesPage() {
    m_profileManager = new ProfileManagerWidget(true, &m_refWebClient, &m_refSerial, this);
    connect(m_profileManager, &ProfileManagerWidget::referenceModeApplyRequested,
            this, &HostWindow::onReferenceModeApplyRequested);
    connect(m_profileManager, &ProfileManagerWidget::logMessage,
            this, &HostWindow::appendLog);
    m_pageProfiles = m_profileManager;
}

void HostWindow::setupHistoryPage() {
    m_pageHistory = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageHistory);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(8);

    QHBoxLayout *filterRow = new QHBoxLayout();
    QLabel *lblSearch = new QLabel("Seri No Ara:", m_pageHistory);
    m_txtSearchSn = new QLineEdit(m_pageHistory);
    m_txtSearchSn->setPlaceholderText("Barkod / Seri No...");
    connect(m_txtSearchSn, &QLineEdit::returnPressed, this, &HostWindow::onRefreshHistoryClicked);

    QPushButton *btnSearch = new QPushButton("🔍 Filtrele / Yenile", m_pageHistory);
    connect(btnSearch, &QPushButton::clicked, this, &HostWindow::onRefreshHistoryClicked);

    QPushButton *btnExportExcel = new QPushButton("📄 Excel Kalite Raporu Üret", m_pageHistory);
    btnExportExcel->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(btnExportExcel, &QPushButton::clicked, this, &HostWindow::onExportExcelClicked);

    filterRow->addWidget(lblSearch);
    filterRow->addWidget(m_txtSearchSn, 1);
    filterRow->addWidget(btnSearch);
    filterRow->addStretch();
    filterRow->addWidget(btnExportExcel);
    vbox->addLayout(filterRow);

    m_tableHistory = new QTableWidget(m_pageHistory);
    m_tableHistory->setColumnCount(10);
    m_tableHistory->setHorizontalHeaderLabels({"ID", "Seri No", "Testi Yapan", "İş Emri", "Masa", "Tarih/Saat", "Süre (sn)", "Sonuç", "Ort. Delta", "Max Delta"});
    m_tableHistory->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableHistory->verticalHeader()->setVisible(false);
    m_tableHistory->setAlternatingRowColors(true);
    m_tableHistory->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableHistory->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vbox->addWidget(m_tableHistory);
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

    QPushButton *btnSave = new QPushButton("💾 Ayarları Kaydet", grpTestSettings);
    btnSave->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold;");
    connect(btnSave, &QPushButton::clicked, this, &HostWindow::onSaveSettingsClicked);

    testForm->addRow("Maksimum Kilit Zaman Aşımı:", m_spinLockTimeout);
    testForm->addRow("Kanal Başına Örnekleme Süresi:", m_spinSampleDuration);
    testForm->addRow("Varsayılan Tolerans Payı:", m_spinTolerance);
    testForm->addRow(btnSave);
    cardsGrid->addWidget(grpTestSettings, 0, 0);

    // Card 2: Reference DRX Ethernet (Primary)
    QGroupBox *grpRefEth = new QGroupBox("🌐 Referans DRX9000 Ethernet & Web CGI Ayarları", m_pageSettings);
    QFormLayout *refForm = new QFormLayout(grpRefEth);
    refForm->setSpacing(10);

    m_txtRefDrxIp = new QLineEdit(AppConfig::instance().refDrxHost(), grpRefEth);
    m_txtRefDrxUser = new QLineEdit(AppConfig::instance().drxUsername(), grpRefEth);
    m_txtRefDrxPass = new QLineEdit(AppConfig::instance().drxPassword(), grpRefEth);
    m_txtRefDrxPass->setEchoMode(QLineEdit::Password);

    QPushButton *btnSaveRefEth = new QPushButton("🔗 Kaydet & Ref Ping Testi Yap", grpRefEth);
    btnSaveRefEth->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnSaveRefEth, &QPushButton::clicked, this, &HostWindow::onSaveRefWebSettingsClicked);

    refForm->addRow("Referans (Golden) DRX IP:", m_txtRefDrxIp);
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
    m_txtDbName = new QLineEdit(AppConfig::instance().dbName(), grpDbSettings);
    dbForm->addRow("Sunucu Host IP:", m_txtDbHost);
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
    cfg.setDrxUsername(m_txtRefDrxUser->text().trimmed());
    cfg.setDrxPassword(m_txtRefDrxPass->text());
    cfg.save();

    m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
    m_refWebClient.setCredentials(cfg.drxUsername(), cfg.drxPassword());
    if (m_lblRefIpBadge) {
        m_lblRefIpBadge->setText(QString("🌐 Ref IP: %1").arg(cfg.refDrxHost()));
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
    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: BAĞLANDI (BOŞTA)");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelBlueStyle());
        m_stationWidgets[stationId].lblIp->setText("IP Adresi: " + ip);
    }
    appendLog(QString("İstasyon %1 ağa bağlandı (%2)").arg(stationId).arg(ip));
}

void HostWindow::onStationDisconnected(int stationId) {
    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: BAĞLI DEĞİL");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPendingBadgeStyle());
    }
    appendLog(QString("İstasyon %1 ağdan ayrıldı.").arg(stationId));
}

void HostWindow::onStationStatusUpdated(const StationLiveStatus &status) {
    int stId = status.stationId;
    if (m_stationWidgets.contains(stId)) {
        m_stationWidgets[stId].lblSn->setText(status.currentAntennaSn.isEmpty() ? "Anten Barkodu: --" : "Anten Barkodu: " + status.currentAntennaSn);
        m_stationWidgets[stId].lblChannel->setText(status.currentChannel > 0 ? QString("Aktif Kanal: %1 / 10").arg(status.currentChannel) : "Aktif Kanal: --");
    }
}

void HostWindow::onLockGranted(int stationId, int timeoutSec) {
    m_lblArbiterStatus->setText(QString("🔒 KİLİTLİ: İstasyon %1 Test Yapıyor (Süre Sınırı: %2 sn)").arg(stationId).arg(timeoutSec));
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #FBBF24;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: TESTTE (REFERANS KİLİTLİ)");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelAmberStyle());
    }
}

void HostWindow::onLockReleased(int stationId) {
    m_lblArbiterStatus->setText("🟢 Referans Alıcısı BOŞTA (Tüm İstasyonlar İçin Müsait)");
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: BOŞTA");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelGreenStyle());
    }
}

void HostWindow::onLockExpired(int stationId) {
    appendLog(QString("UYARI: İstasyon %1 için kilit süresi aşıldı ve düşürüldü!").arg(stationId));
    onLockReleased(stationId);
}

void HostWindow::onRefreshHistoryClicked() {
    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        db.connectToDatabase(AppConfig::instance().dbHost(), AppConfig::instance().dbPort(),
                             AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                             AppConfig::instance().dbPassword());
    }

    auto runs = db.getTestRuns(100, m_txtSearchSn->text().trimmed());
    m_tableHistory->setRowCount(runs.size());

    for (int i = 0; i < runs.size(); ++i) {
        const auto &r = runs[i];
        m_tableHistory->setItem(i, 0, new QTableWidgetItem(QString::number(r.id)));
        m_tableHistory->setItem(i, 1, new QTableWidgetItem(r.antennaSn));
        m_tableHistory->setItem(i, 2, new QTableWidgetItem(r.testerName.isEmpty() ? "--" : r.testerName));
        m_tableHistory->setItem(i, 3, new QTableWidgetItem(QString::number(r.workOrderId)));
        m_tableHistory->setItem(i, 4, new QTableWidgetItem(QString("İstasyon %1").arg(r.stationId)));
        m_tableHistory->setItem(i, 5, new QTableWidgetItem(r.timestamp.toString("dd.MM.yyyy hh:mm:ss")));
        m_tableHistory->setItem(i, 6, new QTableWidgetItem(QString::number(r.durationSec)));

        QTableWidgetItem *resItem = new QTableWidgetItem(r.overallResult);
        if (r.overallResult == "PASS") {
            resItem->setForeground(QBrush(QColor("#34D399")));
        } else {
            resItem->setForeground(QBrush(QColor("#F87171")));
        }
        m_tableHistory->setItem(i, 7, resItem);
        m_tableHistory->setItem(i, 8, new QTableWidgetItem(QString::number(r.avgDeltaDb, 'f', 2) + " dB"));
        m_tableHistory->setItem(i, 9, new QTableWidgetItem(QString::number(r.maxDeltaDb, 'f', 2) + " dB"));
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

void HostWindow::onExportExcelClicked() {
    QString fileName = QFileDialog::getSaveFileName(this, "Excel Raporunu Kaydet", "NETA_Kalite_Raporu.xlsx", "Excel Dosyaları (*.xlsx)");
    if (fileName.isEmpty()) return;

    QStringList headers;
    for (int c = 0; c < m_tableHistory->columnCount(); ++c) {
        auto hItem = m_tableHistory->horizontalHeaderItem(c);
        headers << (hItem ? hItem->text() : QString("Sütun %1").arg(c + 1));
    }

    QVector<QStringList> rows;
    for (int r = 0; r < m_tableHistory->rowCount(); ++r) {
        QStringList rowData;
        for (int c = 0; c < m_tableHistory->columnCount(); ++c) {
            auto item = m_tableHistory->item(r, c);
            rowData << (item ? item->text() : "");
        }
        rows.append(rowData);
    }

    QString errStr;
    if (writeXlsxSheet(fileName, "Kalite Test Raporu", headers, rows, &errStr)) {
        QMessageBox::information(this, "Başarılı", "Excel kalite raporu başarıyla kaydedildi:\n" + fileName);
    } else {
        QMessageBox::critical(this, "Hata", "Excel dosyası oluşturulamadı:\n" + errStr);
    }
}

void HostWindow::onSaveSettingsClicked() {
    AppConfig::instance().setLockTimeoutSec(m_spinLockTimeout->value());
    AppConfig::instance().setSampleDurationSec(m_spinSampleDuration->value());
    AppConfig::instance().setDefaultToleranceDb(m_spinTolerance->value());
    if (m_txtDbHost) AppConfig::instance().setDbHost(m_txtDbHost->text().trimmed());
    if (m_txtDbName) AppConfig::instance().setDbName(m_txtDbName->text().trimmed());
    AppConfig::instance().save();

    QMessageBox::information(this, "Kaydedildi", "Sistem parametreleri başarıyla güncellendi.");
}

void HostWindow::onInstallMysqlClicked() {
    DependencyInstaller::autoSetupHostEnvironment(this);
    onCheckDbClicked();
    onRefreshHistoryClicked();
}

void HostWindow::onResetDbClicked() {
    DependencyInstaller::runResetWithElevation(this);
    DbManager::instance().ensureDefaultDataIfMissing();
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
        if (AppConfig::instance().isClient()) {
            ClientWindow *clientWin = new ClientWindow();
            clientWin->showMaximized();
            this->close();
        } else {
            m_lblOperatorName->setText(AppConfig::instance().currentUserFullName());
            setStatusMessage("Yönetici oturumu yenilendi: " + AppConfig::instance().currentUserFullName());
        }
    }
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
