#include "host_window.h"
#include "appinfo.h"
#include "app_config.h"
#include "build_config.h"
#include "database/db_manager.h"
#include "core/dependency_installer.h"
#include "ui/common/style_helper.h"
#include "ui/common/about_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>

HostWindow::HostWindow(QWidget *parent)
    : QMainWindow(parent),
      m_tcpServer(&m_arbiter, &m_refSerial, this),
      m_beaconBroadcaster(AppConfig::instance().udpBeaconPort(), AppConfig::instance().tcpPort(), this) {
    
#if defined(NETA_ALGORITMA_1_FOUR_PC)
    QString algoTitle = "[ALGORİTMA 1: 4 PC (Yönetici Sunucu)]";
#elif defined(NETA_ALGORITMA_2_THREE_PC)
    QString algoTitle = "[ALGORİTMA 2: 3 PC (İstasyon Paneli)]";
#else
    QString algoTitle = "";
#endif

    setWindowTitle(QString("%1 - Fabrika Yönetim ve Referans Sunucusu (HOST) %2 v%3")
                       .arg(NETA_APP_TITLE)
                       .arg(algoTitle)
                       .arg(NETA_APP_VERSION));
    setMinimumSize(1280, 800);
    setWindowState(Qt::WindowMaximized);

    setupUi();

    // Sinyal Bağlantıları
    connect(&m_tcpServer, &TcpServer::stationConnected, this, &HostWindow::onStationConnected);
    connect(&m_tcpServer, &TcpServer::stationDisconnected, this, &HostWindow::onStationDisconnected);
    connect(&m_tcpServer, &TcpServer::stationStatusUpdated, this, &HostWindow::onStationStatusUpdated);
    connect(&m_tcpServer, &TcpServer::logMessage, this, &HostWindow::appendLog);

    connect(&m_arbiter, &LockArbiter::lockGranted, this, &HostWindow::onLockGranted);
    connect(&m_arbiter, &LockArbiter::lockReleased, this, &HostWindow::onLockReleased);
    connect(&m_arbiter, &LockArbiter::lockExpired, this, &HostWindow::onLockExpired);

    connect(&m_refSerial, &SerialWorker::portOpened, this, [this](const QString &p) {
        m_btnConnectRefSerial->setText("Bağlantıyı Kes");
        m_btnConnectRefSerial->setStyleSheet("background-color: #C62828;");
        appendLog("Referans DRX portu açıldı: " + p);
    });
    connect(&m_refSerial, &SerialWorker::portClosed, this, [this]() {
        m_btnConnectRefSerial->setText("Bağlan");
        m_btnConnectRefSerial->setStyleSheet("background-color: #1976D2;");
        appendLog("Referans DRX portu kapatıldı.");
    });
    connect(&m_refSerial, &SerialWorker::deviceReadyStateChanged, this, [this](DeviceReadyState state, const QString &desc) {
        if (state == DeviceReadyState::Ready) {
            m_lblRefTelemetry->setText("🟢 Referans DRX Hazır");
            m_lblRefTelemetry->setStyleSheet("color: #4CAF50; font-weight: bold;");
        } else if (state == DeviceReadyState::Booting) {
            m_lblRefTelemetry->setText("🟡 Referans DRX Açılıyor (Boot)...");
            m_lblRefTelemetry->setStyleSheet("color: #FFA726; font-weight: bold;");
        } else if (state == DeviceReadyState::PortOpenWaitingDevice) {
            m_lblRefTelemetry->setText("⚪ Referans DRX Bekleniyor (Açınız)");
            m_lblRefTelemetry->setStyleSheet("color: #90CAF9; font-weight: bold;");
        } else {
            m_lblRefTelemetry->setText("🔴 Referans DRX Kapalı");
            m_lblRefTelemetry->setStyleSheet("color: #EF5350; font-weight: bold;");
        }
        appendLog(QString("Referans Alıcı Durumu: %1 (%2)").arg(m_refSerial.readyStateString(), desc));
    });
    connect(&m_refSerial, &SerialWorker::telemetryReceived, this, [this](double powDbm, double cnDb, double /*ber*/) {
        m_lblRefTelemetry->setText(QString("Canlı: C/N=%1 dB | POW=%2 dBm").arg(cnDb, 0, 'f', 1).arg(powDbm, 0, 'f', 1));
    });

    // TCP Sunucu ve UDP Beacon Başlat
    m_tcpServer.start(AppConfig::instance().tcpPort());
    m_beaconBroadcaster.start(2000);
    appendLog(QString("Lobi yayını başladı (UDP %1, TCP %2).").arg(AppConfig::instance().udpBeaconPort()).arg(AppConfig::instance().tcpPort()));

    onRefreshPortsClicked();
    if (DbManager::instance().isConnected()) {
        DbManager::instance().ensureDefaultDataIfMissing();
    }
    onRefreshHistoryClicked();
}

HostWindow::~HostWindow() {
    m_beaconBroadcaster.stop();
    m_tcpServer.stop();
    m_refSerial.closePort();
}

void HostWindow::setupUi() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // --- 1. ÜST SUNUCU & REFERANS DRX BAĞLANTI ÇUBUĞU ---
    QFrame *topBar = new QFrame(this);
    topBar->setStyleSheet("background-color: #1A1F26; border-radius: 8px; padding: 6px;");
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);

    QLabel *lblTitle = new QLabel("🏢 <b>NETA DRX9000 YÖNETİM & REFERANS SUNUCUSU</b>", this);
    lblTitle->setStyleSheet("font-size: 16px; color: #64B5F6;");

    m_lblServerStatus = new QLabel(QString("🟢 Sunucu Aktif (Port: %1)").arg(AppConfig::instance().tcpPort()), this);
    m_lblServerStatus->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; border-radius: 12px; padding: 4px 12px; font-weight: bold;");

    QLabel *lblRefPort = new QLabel("Referans DRX Portu:", this);
    m_comboSerialPorts = new QComboBox(this);
    m_comboSerialPorts->setMinimumWidth(100);

    QPushButton *btnRefresh = new QPushButton("🔄", this);
    btnRefresh->setFixedWidth(36);
    connect(btnRefresh, &QPushButton::clicked, this, &HostWindow::onRefreshPortsClicked);

    m_btnConnectRefSerial = new QPushButton("Bağlan", this);
    connect(m_btnConnectRefSerial, &QPushButton::clicked, this, &HostWindow::onConnectRefSerialClicked);

    m_lblRefTelemetry = new QLabel("Referans Telemetri: Bekleniyor...", this);
    m_lblRefTelemetry->setStyleSheet("color: #FFB300; font-weight: bold;");

    topLayout->addWidget(lblTitle);
    topLayout->addSpacing(15);
    topLayout->addWidget(m_lblServerStatus);
    topLayout->addStretch();
    topLayout->addWidget(lblRefPort);
    topLayout->addWidget(m_comboSerialPorts);
    topLayout->addWidget(btnRefresh);
    topLayout->addWidget(m_btnConnectRefSerial);
    topLayout->addSpacing(15);
    topLayout->addWidget(m_lblRefTelemetry);

    QPushButton *btnAbout = new QPushButton("ℹ️ Hakkında", this);
    btnAbout->setFixedHeight(26);
    btnAbout->setCursor(Qt::PointingHandCursor);
    btnAbout->setStyleSheet(
        "QPushButton { background-color: #1E2530; color: #90CAF9; border: 1px solid #37474F; border-radius: 4px; padding: 2px 10px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background-color: #263238; border-color: #64B5F6; color: #FFFFFF; }"
    );
    connect(btnAbout, &QPushButton::clicked, this, &HostWindow::onAboutClicked);
    topLayout->addSpacing(12);
    topLayout->addWidget(btnAbout);

    mainLayout->addWidget(topBar);

    // --- 2. SEKMELER (TAB WIDGET) ---
    QTabWidget *tabWidget = new QTabWidget(this);
    setupLobbyTab();
    setupHistoryTab();
    setupOrdersTab();
    setupSettingsTab();

    tabWidget->addTab(findChild<QWidget*>("lobbyTab"), "🎮 Canlı Test Lobisi & Masalar");
    tabWidget->addTab(findChild<QWidget*>("historyTab"), "📊 Test Geçmişi & Kalite Kayıtları");
    tabWidget->addTab(findChild<QWidget*>("ordersTab"), "📦 İş Emirleri & Sipariş Havuzu");
    tabWidget->addTab(findChild<QWidget*>("settingsTab"), "⚙️ Sistem & Tolerans Ayarları");
    mainLayout->addWidget(tabWidget, 80);

    // --- 3. ALT LOG PENCERESİ & FOOTER ---
    m_txtLog = new QTextEdit(this);
    m_txtLog->setReadOnly(true);
    m_txtLog->setMaximumHeight(100);
    m_txtLog->setStyleSheet("background-color: #0E1116; color: #81C784; font-family: Consolas, monospace; font-size: 11px;");
    mainLayout->addWidget(m_txtLog, 20);

    QHBoxLayout *footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(4, 2, 4, 0);
    QLabel *lblFooterApp = new QLabel(QString("%1 v%2").arg(NETA_APP_NAME, NETA_APP_VERSION), this);
    lblFooterApp->setStyleSheet("color: #607D8B; font-size: 11px;");
    QLabel *lblFooterDev = new QLabel(QString("Geliştirici: <b>%1</b> | %2").arg(NETA_APP_DEVELOPER, NETA_APP_ORGANIZATION), this);
    lblFooterDev->setStyleSheet("color: #78909C; font-size: 11px;");
    footerLayout->addWidget(lblFooterApp);
    footerLayout->addStretch();
    footerLayout->addWidget(lblFooterDev);
    mainLayout->addLayout(footerLayout);
}

void HostWindow::setupLobbyTab() {
    QWidget *tab = new QWidget(this);
    tab->setObjectName("lobbyTab");
    QVBoxLayout *vbox = new QVBoxLayout(tab);

    // Referans Hakemi & Dağıtık Kilit Kartı
    QGroupBox *grpArbiter = new QGroupBox("Merkezi Referans Kilit Durumu (Mutex Arbiter)", tab);
    QHBoxLayout *arbLayout = new QHBoxLayout(grpArbiter);
    m_lblArbiterStatus = new QLabel("🟢 Referans Alıcısı BOŞTA (Tüm İstasyonlar İçin Müsait)", grpArbiter);
    m_lblArbiterStatus->setStyleSheet("font-size: 15px; font-weight: bold; color: #4CAF50;");
    arbLayout->addWidget(m_lblArbiterStatus);
    vbox->addWidget(grpArbiter);

    // 3 Test İstasyonu Kartları
    QGroupBox *grpStations = new QGroupBox("Üretim Hattındaki Test Masaları", tab);
    QHBoxLayout *stLayout = new QHBoxLayout(grpStations);

    for (int stId = 1; stId <= 3; ++stId) {
        QFrame *card = new QFrame(grpStations);
        card->setStyleSheet("background-color: #242B35; border: 1px solid #3A4659; border-radius: 8px; padding: 10px;");
        QVBoxLayout *cardVBox = new QVBoxLayout(card);

        QLabel *lblTitle = new QLabel(QString("🖥️ <b>İSTASYON %1</b>").arg(stId), card);
        lblTitle->setStyleSheet("font-size: 15px; color: #64B5F6;");

        StationCardWidgets widgets;
        widgets.lblState = new QLabel("DURUM: BAĞLI DEĞİL", card);
        widgets.lblState->setStyleSheet(StyleHelper::getPendingBadgeStyle());

        widgets.lblIp = new QLabel("IP: --", card);
        widgets.lblIp->setStyleSheet("color: #9CA3AF;");

        widgets.lblSn = new QLabel("Anten: --", card);
        widgets.lblSn->setStyleSheet("font-weight: bold; color: #FFF;");

        widgets.lblChannel = new QLabel("Kanal: --", card);
        widgets.lblChannel->setStyleSheet("color: #B0BEC5;");

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

void HostWindow::setupHistoryTab() {
    QWidget *tab = new QWidget(this);
    tab->setObjectName("historyTab");
    QVBoxLayout *vbox = new QVBoxLayout(tab);

    QHBoxLayout *filterRow = new QHBoxLayout();
    QLabel *lblSearch = new QLabel("Seri No Ara:", tab);
    m_txtSearchSn = new QLineEdit(tab);
    m_txtSearchSn->setPlaceholderText("Barkod / Seri No...");
    QPushButton *btnSearch = new QPushButton("Filtrele", tab);
    connect(btnSearch, &QPushButton::clicked, this, &HostWindow::onRefreshHistoryClicked);

    QPushButton *btnExportExcel = new QPushButton("📄 Resmi Excel Kalite Raporu Üret", tab);
    btnExportExcel->setStyleSheet("background-color: #2E7D32; font-weight: bold;");
    connect(btnExportExcel, &QPushButton::clicked, this, &HostWindow::onExportExcelClicked);

    filterRow->addWidget(lblSearch);
    filterRow->addWidget(m_txtSearchSn);
    filterRow->addWidget(btnSearch);
    filterRow->addStretch();
    filterRow->addWidget(btnExportExcel);
    vbox->addLayout(filterRow);

    m_tableHistory = new QTableWidget(tab);
    m_tableHistory->setColumnCount(9);
    m_tableHistory->setHorizontalHeaderLabels({"ID", "Seri No", "İş Emri", "Masa", "Tarih/Saat", "Süre (sn)", "Sonuç", "Ort. Delta", "Max Delta"});
    m_tableHistory->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableHistory->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableHistory->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vbox->addWidget(m_tableHistory);
}

void HostWindow::setupOrdersTab() {
    QWidget *tab = new QWidget(this);
    tab->setObjectName("ordersTab");
    QVBoxLayout *vbox = new QVBoxLayout(tab);

    m_tableOrders = new QTableWidget(tab);
    m_tableOrders->setColumnCount(7);
    m_tableOrders->setHorizontalHeaderLabels({"Sipariş No", "Müşteri", "Ürün Kodu", "Hedef Adet", "Tamamlanan", "Başarılı", "Durum"});
    m_tableOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableOrders->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableOrders->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vbox->addWidget(m_tableOrders);
}

void HostWindow::setupSettingsTab() {
    QWidget *tab = new QWidget(this);
    tab->setObjectName("settingsTab");
    QVBoxLayout *vbox = new QVBoxLayout(tab);

    // 1. Test ve Tolerans Ayarları
    QGroupBox *grpTestSettings = new QGroupBox("Test ve Tolerans Parametreleri", tab);
    QGridLayout *grid = new QGridLayout(grpTestSettings);

    grid->addWidget(new QLabel("Maksimum Kilit Zaman Aşımı (sn):", grpTestSettings), 0, 0);
    m_spinLockTimeout = new QSpinBox(grpTestSettings);
    m_spinLockTimeout->setRange(10, 600);
    m_spinLockTimeout->setValue(AppConfig::instance().lockTimeoutSec());
    grid->addWidget(m_spinLockTimeout, 0, 1);

    grid->addWidget(new QLabel("Kanal Başına Örnekleme Süresi (sn):", grpTestSettings), 1, 0);
    m_spinSampleDuration = new QSpinBox(grpTestSettings);
    m_spinSampleDuration->setRange(1, 30);
    m_spinSampleDuration->setValue(AppConfig::instance().sampleDurationSec());
    grid->addWidget(m_spinSampleDuration, 1, 1);

    grid->addWidget(new QLabel("Varsayılan İzin Verilen Tolerans (dB):", grpTestSettings), 2, 0);
    m_spinTolerance = new QDoubleSpinBox(grpTestSettings);
    m_spinTolerance->setRange(0.1, 10.0);
    m_spinTolerance->setSingleStep(0.1);
    m_spinTolerance->setValue(AppConfig::instance().defaultToleranceDb());
    grid->addWidget(m_spinTolerance, 2, 1);

    vbox->addWidget(grpTestSettings);

    // 2. Merkezi Veritabanı ve Sunucu Bağımlılıkları
    QGroupBox *grpDbSettings = new QGroupBox("Merkezi Veritabanı ve Sunucu Bağımlılıkları (MySQL / MariaDB)", tab);
    QVBoxLayout *vboxDb = new QVBoxLayout(grpDbSettings);

    QHBoxLayout *statusRow = new QHBoxLayout();
    QLabel *lblDbStatusTitle = new QLabel("MySQL Servis Durumu:", grpDbSettings);
    lblDbStatusTitle->setStyleSheet("font-weight: bold; font-size: 13px;");
    m_lblDbStatus = new QLabel("Kontrol ediliyor...", grpDbSettings);
    m_lblDbStatus->setStyleSheet("font-size: 13px; font-weight: bold; color: #FFA726;");
    QPushButton *btnCheckDb = new QPushButton("🔄 Kontrol Et", grpDbSettings);
    btnCheckDb->setFixedHeight(28);
    connect(btnCheckDb, &QPushButton::clicked, this, &HostWindow::onCheckDbClicked);

    statusRow->addWidget(lblDbStatusTitle);
    statusRow->addWidget(m_lblDbStatus);
    statusRow->addStretch();
    statusRow->addWidget(btnCheckDb);
    vboxDb->addLayout(statusRow);

    QGridLayout *dbGrid = new QGridLayout();
    dbGrid->addWidget(new QLabel("Veritabanı Sunucu Adresi (Host IP):", grpDbSettings), 0, 0);
    m_txtDbHost = new QLineEdit(grpDbSettings);
    m_txtDbHost->setText(AppConfig::instance().dbHost());
    dbGrid->addWidget(m_txtDbHost, 0, 1);

    dbGrid->addWidget(new QLabel("Veritabanı Adı:", grpDbSettings), 1, 0);
    m_txtDbName = new QLineEdit(grpDbSettings);
    m_txtDbName->setText(AppConfig::instance().dbName());
    dbGrid->addWidget(m_txtDbName, 1, 1);
    vboxDb->addLayout(dbGrid);

    QHBoxLayout *dbActionRow = new QHBoxLayout();
    QPushButton *btnInstallMysql = new QPushButton("⚡ Tek Tıkla MariaDB / MySQL Servisini Kur", grpDbSettings);
    btnInstallMysql->setStyleSheet("background-color: #E65100; color: #FFFFFF; font-weight: bold; padding: 8px 16px; border-radius: 4px;");
    connect(btnInstallMysql, &QPushButton::clicked, this, &HostWindow::onInstallMysqlClicked);

    QPushButton *btnResetDb = new QPushButton("📦 Tabloları ve Seed Verisini Sıfırla / Yenile", grpDbSettings);
    btnResetDb->setStyleSheet("background-color: #37474F; color: #ECEFF1; font-weight: bold; padding: 8px 16px; border-radius: 4px;");
    connect(btnResetDb, &QPushButton::clicked, this, &HostWindow::onResetDbClicked);

    dbActionRow->addWidget(btnInstallMysql);
    dbActionRow->addWidget(btnResetDb);
    dbActionRow->addStretch();
    vboxDb->addLayout(dbActionRow);

    vbox->addWidget(grpDbSettings);

    QPushButton *btnSave = new QPushButton("Ayarları Kaydet", tab);
    btnSave->setStyleSheet("background-color: #1976D2; font-weight: bold; padding: 10px; font-size: 13px;");
    connect(btnSave, &QPushButton::clicked, this, &HostWindow::onSaveSettingsClicked);
    vbox->addWidget(btnSave);
    vbox->addStretch();

    // Başlangıç durum kontrolü
    onCheckDbClicked();
}

void HostWindow::onRefreshPortsClicked() {
    m_comboSerialPorts->clear();
    QStringList ports = SerialWorker::availablePorts();
    m_comboSerialPorts->addItems(ports);

    int idx = m_comboSerialPorts->findText(AppConfig::instance().serialPort());
    if (idx >= 0) m_comboSerialPorts->setCurrentIndex(idx);
}

void HostWindow::onConnectRefSerialClicked() {
    if (m_refSerial.isConnected()) {
        m_refSerial.closePort();
    } else {
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
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getActiveBadgeStyle());
        m_stationWidgets[stationId].lblIp->setText("IP: " + ip);
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
        m_stationWidgets[stId].lblSn->setText(status.currentAntennaSn.isEmpty() ? "Anten: --" : "Anten: " + status.currentAntennaSn);
        m_stationWidgets[stId].lblChannel->setText(status.currentChannel > 0 ? QString("Kanal: %1 / 10").arg(status.currentChannel) : "Kanal: --");
    }
}

void HostWindow::onLockGranted(int stationId, int timeoutSec) {
    m_lblArbiterStatus->setText(QString("🔒 KİLİTLİ: İstasyon %1 Test Yapıyor (Kalan Süre: %2 sn)").arg(stationId).arg(timeoutSec));
    m_lblArbiterStatus->setStyleSheet("font-size: 15px; font-weight: bold; color: #E65100;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: TEST YAPILIYOR (KİLİTLİ)");
        m_stationWidgets[stationId].lblState->setStyleSheet("background-color: #EF6C00; color: #FFF; font-weight: bold; border-radius: 6px; padding: 4px;");
    }
}

void HostWindow::onLockReleased(int stationId) {
    m_lblArbiterStatus->setText("🟢 Referans Alıcısı BOŞTA (Tüm İstasyonlar İçin Müsait)");
    m_lblArbiterStatus->setStyleSheet("font-size: 15px; font-weight: bold; color: #4CAF50;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: BOŞTA");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getActiveBadgeStyle());
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
        m_tableHistory->setItem(i, 2, new QTableWidgetItem(QString::number(r.workOrderId)));
        m_tableHistory->setItem(i, 3, new QTableWidgetItem(QString("İstasyon %1").arg(r.stationId)));
        m_tableHistory->setItem(i, 4, new QTableWidgetItem(r.timestamp.toString("dd.MM.yyyy hh:mm:ss")));
        m_tableHistory->setItem(i, 5, new QTableWidgetItem(QString::number(r.durationSec)));

        QTableWidgetItem *resItem = new QTableWidgetItem(r.overallResult);
        if (r.overallResult == "PASS") {
            resItem->setForeground(QBrush(QColor(76, 175, 80)));
        } else {
            resItem->setForeground(QBrush(QColor(244, 67, 54)));
        }
        m_tableHistory->setItem(i, 6, resItem);
        m_tableHistory->setItem(i, 7, new QTableWidgetItem(QString::number(r.avgDeltaDb, 'f', 2) + " dB"));
        m_tableHistory->setItem(i, 8, new QTableWidgetItem(QString::number(r.maxDeltaDb, 'f', 2) + " dB"));
    }

    // Siparişleri de güncelle
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
    if (m_txtDbHost) {
        AppConfig::instance().setDbHost(m_txtDbHost->text().trimmed());
    }
    if (m_txtDbName) {
        AppConfig::instance().setDbName(m_txtDbName->text().trimmed());
    }
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
            m_lblDbStatus->setText("✔ MySQL Servisi Aktif (Port 3306)");
            m_lblDbStatus->setStyleSheet("color: #4CAF50; font-weight: bold; font-size: 13px;");
        } else {
            m_lblDbStatus->setText("❌ MySQL Servisi Kapalı / Dinlenmiyor");
            m_lblDbStatus->setStyleSheet("color: #F44336; font-weight: bold; font-size: 13px;");
        }
    }
}

void HostWindow::appendLog(const QString &msg) {
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_txtLog->append(QString("[%1] %2").arg(timeStr, msg));
}

void HostWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}
