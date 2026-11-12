/**
 * @file lobby_window.cpp
 * @brief Implementation of the unified HOST / JOIN multiplayer-style server lobby window.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "lobby_window.h"
#include "core/appinfo.h"
#include "core/app_config.h"
#include "core/dependency_installer.h"
#include "database/db_manager.h"
#include "ui/common/style_helper.h"
#include "ui/common/login_dialog.h"
#include "ui/common/about_dialog.h"
#include "ui/host/host_window.h"
#include "ui/client/client_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QHostInfo>
#include <QPixmap>

LobbyWindow::LobbyWindow(QWidget *parent)
    : QMainWindow(parent),
      m_beaconListener(AppConfig::instance().udpBeaconPort(), this) {

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString("%1 - Sunucu Lobisi (HOST / JOIN) v%2").arg(NETA_APP_TITLE, NETA_APP_VERSION));
    setMinimumSize(1020, 620);

    setupUi();

    connect(&m_beaconListener, &UdpBeaconListener::hostDiscoveredInfo,
            this, &LobbyWindow::onHostDiscoveredInfo);
    m_beaconListener.start();

    connect(&m_cleanupTimer, &QTimer::timeout, this, &LobbyWindow::onCleanupStaleServers);
    m_cleanupTimer.start(2000);
}

LobbyWindow::~LobbyWindow() {
    m_cleanupTimer.stop();
    m_beaconListener.stop();
}

void LobbyWindow::setupUi() {
    QMenu *menuOturum = menuBar()->addMenu("Oturum");
    QAction *actSwitch = menuOturum->addAction("Kullanıcı Değiştir");
    connect(actSwitch, &QAction::triggered, this, &LobbyWindow::onSwitchUserClicked);
    menuOturum->addSeparator();
    QAction *actQuit = menuOturum->addAction("Çıkış");
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    QMenu *menuYardim = menuBar()->addMenu("Yardım");
    QAction *actAbout = menuYardim->addAction("Hakkında");
    connect(actAbout, &QAction::triggered, this, [this]() { AboutDialog(this).exec(); });

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *rootVBox = new QVBoxLayout(central);
    rootVBox->setContentsMargins(18, 14, 18, 14);
    rootVBox->setSpacing(14);

    // ── 1. Üst Karşılama & Kullanıcı Başlığı ─────────────────────────────────
    QFrame *headerCard = new QFrame(central);
    headerCard->setStyleSheet("background-color: #0A1F17; border: 1px solid #1A3D2B; border-radius: 8px; padding: 10px 14px;");
    QHBoxLayout *headerHBox = new QHBoxLayout(headerCard);
    headerHBox->setSpacing(12);

    QLabel *lblLogo = new QLabel(headerCard);
    lblLogo->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 4px 10px;");
    QPixmap pix(":/resources/images/logo.png");
    if (!pix.isNull()) {
        lblLogo->setPixmap(pix.scaled(124, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        lblLogo->setText("<b style='color:#064E3B; font-size:14pt;'>NETA</b>");
    }

    QVBoxLayout *titleCol = new QVBoxLayout();
    QLabel *lblMain = new QLabel("NETA DRX9000 — AĞ LOBİSİ (SUNUCU KUR / KATIL)", headerCard);
    lblMain->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");
    QLabel *lblSub = new QLabel("Yönetici PC iseniz sunucu başlatın (HOST); test istasyonu iseniz ağdaki açık sunucuya katılın (JOIN).", headerCard);
    lblSub->setStyleSheet("font-size: 9pt; color: #A7C4B5;");
    titleCol->addWidget(lblMain);
    titleCol->addWidget(lblSub);

    AppConfig &cfg = AppConfig::instance();
    m_lblUserBadge = new QLabel(
        QString("👤 %1 (%2)").arg(cfg.currentUserFullName(), cfg.isEngineer() ? "Mühendis" : "Tekniker"),
        headerCard);
    m_lblUserBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());

    QPushButton *btnSwitchUser = new QPushButton("🔄 Kullanıcı Değiştir", headerCard);
    btnSwitchUser->setCursor(Qt::PointingHandCursor);
    connect(btnSwitchUser, &QPushButton::clicked, this, &LobbyWindow::onSwitchUserClicked);

    headerHBox->addWidget(lblLogo);
    headerHBox->addLayout(titleCol, 1);
    headerHBox->addWidget(m_lblUserBadge);
    headerHBox->addWidget(btnSwitchUser);
    rootVBox->addWidget(headerCard);

    // ── 2. Ortada İki Büyük Kart: HOST (Sunucu Kur) | JOIN (Sunucuya Katıl) ─
    QHBoxLayout *cardsHBox = new QHBoxLayout();
    cardsHBox->setSpacing(16);

    // --- SOL KART: HOST ---
    QGroupBox *grpHost = new QGroupBox("🏢 SUNUCU KUR (HOST — Yönetici PC)", central);
    grpHost->setFixedWidth(360);
    QVBoxLayout *hostVBox = new QVBoxLayout(grpHost);
    hostVBox->setSpacing(12);

    QLabel *lblHostDesc = new QLabel(
        "• Bu bilgisayarı merkezi Yönetim Sunucusu ve Referans Hakemi olarak başlatır.\n"
        "• Ağdaki tüm test istasyonları (3 veya daha fazla) bu sunucuyu görüp bağlanabilir.\n"
        "• Taşınabilir MariaDB servisi ve Referans DRX kontrolü bu PC'de çalışır.",
        grpHost);
    lblHostDesc->setWordWrap(true);
    lblHostDesc->setStyleSheet("background-color: #112D21; border: 1px solid #1A3D2B; border-radius: 6px; padding: 10px; color: #E6F4EE;");
    hostVBox->addWidget(lblHostDesc);

    QFormLayout *hostForm = new QFormLayout();
    hostForm->setSpacing(10);
    m_txtServerName = new QLineEdit(cfg.serverName(), grpHost);
    m_txtServerName->setPlaceholderText("Örn: NETA-FABRIKA-HOST");

    m_spinHostPort = new QSpinBox(grpHost);
    m_spinHostPort->setRange(1024, 65535);
    m_spinHostPort->setValue(cfg.tcpPort());

    hostForm->addRow("Sunucu Adı:", m_txtServerName);
    hostForm->addRow("TCP Sunucu Portu:", m_spinHostPort);
    hostVBox->addLayout(hostForm);
    hostVBox->addStretch();

    QPushButton *btnHost = new QPushButton("🚀 SUNUCUYU BAŞLAT (HOST)", grpHost);
    btnHost->setMinimumHeight(46);
    btnHost->setCursor(Qt::PointingHandCursor);
    btnHost->setStyleSheet(
        "QPushButton { background-color: #10B981; color: #071210; border: none; border-radius: 6px; font-weight: bold; font-size: 11pt; }"
        "QPushButton:hover { background-color: #34D399; }"
    );
    connect(btnHost, &QPushButton::clicked, this, &LobbyWindow::onHostServerClicked);
    hostVBox->addWidget(btnHost);

    cardsHBox->addWidget(grpHost);

    // --- SAĞ KART: JOIN ---
    QGroupBox *grpJoin = new QGroupBox("🎮 AĞDAKİ SUNUCUYA KATIL (JOIN — Test İstasyonu)", central);
    QVBoxLayout *joinVBox = new QVBoxLayout(grpJoin);
    joinVBox->setSpacing(10);

    QHBoxLayout *aliasRow = new QHBoxLayout();
    QLabel *lblAlias = new QLabel("Bu İstasyonun Adı:", grpJoin);
    m_txtStationAlias = new QLineEdit(cfg.stationAlias().isEmpty() ? QHostInfo::localHostName() : cfg.stationAlias(), grpJoin);
    m_txtStationAlias->setPlaceholderText("Örn: Masa-4 veya Test-PC");
    QLabel *lblDynHint = new QLabel("(İstasyon # sunucu tarafından otomatik atanır — sınırsız istasyon)", grpJoin);
    lblDynHint->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");
    aliasRow->addWidget(lblAlias);
    aliasRow->addWidget(m_txtStationAlias, 1);
    aliasRow->addWidget(lblDynHint);
    joinVBox->addLayout(aliasRow);

    QLabel *lblServersTitle = new QLabel(
        QString("Aynı Ağda Yayın Yapan Sunucular (UDP Port %1 — Çift tıklayarak katılabilirsiniz):").arg(cfg.udpBeaconPort()),
        grpJoin);
    lblServersTitle->setStyleSheet("color: #34D399; font-weight: bold;");
    joinVBox->addWidget(lblServersTitle);

    m_tableServers = new QTableWidget(grpJoin);
    m_tableServers->setColumnCount(5);
    m_tableServers->setHorizontalHeaderLabels({"Sunucu Adı", "Host IP Adresi", "Port", "Bağlı İstasyon", "Sürüm"});
    m_tableServers->verticalHeader()->setVisible(false);
    m_tableServers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableServers->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableServers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableServers->setAlternatingRowColors(true);
    m_tableServers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tableServers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tableServers->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tableServers->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tableServers->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connect(m_tableServers, &QTableWidget::cellDoubleClicked, this, &LobbyWindow::onServerRowDoubleClicked);
    joinVBox->addWidget(m_tableServers, 1);

    m_btnJoinSelected = new QPushButton("🔗 SEÇİLİ SUNUCUYA KATIL (JOIN)", grpJoin);
    m_btnJoinSelected->setMinimumHeight(44);
    m_btnJoinSelected->setCursor(Qt::PointingHandCursor);
    m_btnJoinSelected->setEnabled(false);
    m_btnJoinSelected->setStyleSheet(
        "QPushButton { background-color: #059669; color: #FFFFFF; border: none; border-radius: 6px; font-weight: bold; font-size: 11pt; }"
        "QPushButton:hover { background-color: #10B981; color: #071210; }"
        "QPushButton:disabled { background-color: #0D2318; color: #3D6652; }"
    );
    connect(m_btnJoinSelected, &QPushButton::clicked, this, &LobbyWindow::onJoinSelectedClicked);
    joinVBox->addWidget(m_btnJoinSelected);

    // Manuel bağlantı fallback (broadcast engelli ağlar için)
    QHBoxLayout *manualRow = new QHBoxLayout();
    QLabel *lblMan = new QLabel("Doğrudan IP ile Katıl:", grpJoin);
    m_txtManualIp = new QLineEdit(cfg.hostIp(), grpJoin);
    m_txtManualIp->setPlaceholderText("192.168.1.xxx");
    m_spinManualPort = new QSpinBox(grpJoin);
    m_spinManualPort->setRange(1024, 65535);
    m_spinManualPort->setValue(cfg.tcpPort());
    QPushButton *btnManualJoin = new QPushButton("IP ile Katıl", grpJoin);
    btnManualJoin->setCursor(Qt::PointingHandCursor);
    connect(btnManualJoin, &QPushButton::clicked, this, &LobbyWindow::onJoinManualClicked);

    manualRow->addWidget(lblMan);
    manualRow->addWidget(m_txtManualIp, 1);
    manualRow->addWidget(m_spinManualPort);
    manualRow->addWidget(btnManualJoin);
    joinVBox->addLayout(manualRow);

    cardsHBox->addWidget(grpJoin, 1);
    rootVBox->addLayout(cardsHBox, 1);

    m_lblStatus = new QLabel(
        QString("Ağda sunucu aranıyor (UDP Port %1)...").arg(cfg.udpBeaconPort()), this);
    m_lblStatus->setStyleSheet("color: #A7C4B5; padding-left: 6px;");
    statusBar()->addWidget(m_lblStatus, 1);
}

void LobbyWindow::onHostDiscoveredInfo(const QString &hostIp, const QString &serverName,
                                       quint16 tcpPort, int stationCount, const QString &version) {
    QString key = QString("%1:%2").arg(hostIp).arg(tcpPort);
    DiscoveredServer srv;
    srv.serverName = serverName.isEmpty() ? hostIp : serverName;
    srv.hostIp = hostIp;
    srv.tcpPort = tcpPort;
    srv.stationCount = stationCount;
    srv.version = version;
    srv.lastSeen = QDateTime::currentDateTime();
    m_servers[key] = srv;

    refreshServerTable();
}

void LobbyWindow::onCleanupStaleServers() {
    QDateTime now = QDateTime::currentDateTime();
    bool removed = false;
    for (auto it = m_servers.begin(); it != m_servers.end();) {
        if (it.value().lastSeen.secsTo(now) > 6) {
            it = m_servers.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed) {
        refreshServerTable();
    }
}

void LobbyWindow::refreshServerTable() {
    if (!m_tableServers) return;

    int prevRow = m_tableServers->currentRow();
    m_tableServers->setRowCount(m_servers.size());

    int r = 0;
    for (auto it = m_servers.constBegin(); it != m_servers.constEnd(); ++it, ++r) {
        const auto &s = it.value();
        m_tableServers->setRowHeight(r, 34);

        auto *itName = new QTableWidgetItem("🏢 " + s.serverName);
        itName->setData(Qt::UserRole, s.hostIp);
        itName->setData(Qt::UserRole + 1, s.tcpPort);

        auto *itIp = new QTableWidgetItem(s.hostIp);
        itIp->setTextAlignment(Qt::AlignCenter);

        auto *itPort = new QTableWidgetItem(QString::number(s.tcpPort));
        itPort->setTextAlignment(Qt::AlignCenter);

        auto *itCnt = new QTableWidgetItem(QString("%1 İstasyon").arg(s.stationCount));
        itCnt->setTextAlignment(Qt::AlignCenter);

        auto *itVer = new QTableWidgetItem(s.version.isEmpty() ? "--" : ("v" + s.version));
        itVer->setTextAlignment(Qt::AlignCenter);

        m_tableServers->setItem(r, 0, itName);
        m_tableServers->setItem(r, 1, itIp);
        m_tableServers->setItem(r, 2, itPort);
        m_tableServers->setItem(r, 3, itCnt);
        m_tableServers->setItem(r, 4, itVer);
    }

    if (m_servers.isEmpty()) {
        m_btnJoinSelected->setEnabled(false);
        m_lblStatus->setText("Ağda açık sunucu aranıyor... (Sunucu kurmak için soldan HOST başlatın)");
    } else {
        m_btnJoinSelected->setEnabled(true);
        int targetRow = (prevRow >= 0 && prevRow < m_tableServers->rowCount()) ? prevRow : 0;
        m_tableServers->selectRow(targetRow);
        m_lblStatus->setText(QString("✔ Ağda %1 aktif sunucu bulundu.").arg(m_servers.size()));
    }
}

void LobbyWindow::onHostServerClicked() {
    launchHostMode();
}

void LobbyWindow::onJoinSelectedClicked() {
    int row = m_tableServers->currentRow();
    if (row < 0 || row >= m_tableServers->rowCount()) return;

    auto *item = m_tableServers->item(row, 0);
    if (!item) return;
    QString hostIp = item->data(Qt::UserRole).toString();
    quint16 tcpPort = static_cast<quint16>(item->data(Qt::UserRole + 1).toUInt());
    launchClientMode(hostIp, tcpPort);
}

void LobbyWindow::onServerRowDoubleClicked(int row, int /*column*/) {
    if (row >= 0) {
        onJoinSelectedClicked();
    }
}

void LobbyWindow::onJoinManualClicked() {
    QString ip = m_txtManualIp->text().trimmed();
    if (ip.isEmpty()) {
        QMessageBox::warning(this, "IP Eksik", "Lütfen bağlanılacak Host sunucunun IP adresini girin.");
        m_txtManualIp->setFocus();
        return;
    }
    launchClientMode(ip, static_cast<quint16>(m_spinManualPort->value()));
}

void LobbyWindow::launchHostMode() {
    AppConfig &cfg = AppConfig::instance();
    cfg.setRole(AppRole::Host);
    if (!m_txtServerName->text().trimmed().isEmpty()) {
        cfg.setServerName(m_txtServerName->text().trimmed());
    }
    cfg.setTcpPort(static_cast<quint16>(m_spinHostPort->value()));
    cfg.setDbHost("127.0.0.1");
    cfg.save();

    bool connected = DbManager::instance().connectToDatabase(cfg.dbHost(), cfg.dbPort(),
                                                            cfg.dbName(), cfg.dbUser(),
                                                            cfg.dbPassword());
    if (!connected && !DependencyInstaller::isMySqlListening(cfg.dbHost(), cfg.dbPort())) {
        DependencyInstaller::autoSetupHostEnvironment(this);
    } else if (connected) {
        DbManager::instance().ensureDefaultDataIfMissing();
    }

    HostWindow *hostWin = new HostWindow();
    hostWin->showMaximized();
    this->close();
}

void LobbyWindow::launchClientMode(const QString &hostIp, quint16 tcpPort) {
    AppConfig &cfg = AppConfig::instance();
    cfg.setRole(AppRole::Client);
    cfg.setHostIp(hostIp);
    cfg.setDbHost(hostIp);
    cfg.setTcpPort(tcpPort);
    cfg.setStationAlias(m_txtStationAlias->text().trimmed());
    cfg.setStationId(0); // Sunucu bağlandığında benzersiz dinamik ID atayacak
    cfg.save();

    DbManager::instance().connectToDatabase(cfg.dbHost(), cfg.dbPort(),
                                            cfg.dbName(), cfg.dbUser(),
                                            cfg.dbPassword());

    ClientWindow *clientWin = new ClientWindow();
    clientWin->showMaximized();
    this->close();
}

void LobbyWindow::onSwitchUserClicked() {
    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        AppConfig &cfg = AppConfig::instance();
        m_lblUserBadge->setText(
            QString("👤 %1 (%2)").arg(cfg.currentUserFullName(), cfg.isEngineer() ? "Mühendis" : "Tekniker"));
    }
}
