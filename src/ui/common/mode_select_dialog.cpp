#include "mode_select_dialog.h"
#include "about_dialog.h"
#include "core/appinfo.h"
#include "core/dependency_installer.h"
#include "database/db_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPixmap>
#include <QMouseEvent>
#include <QMessageBox>
#include <QGraphicsDropShadowEffect>

ModeSelectDialog::ModeSelectDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QString("%1 v%2 - Çalışma Rolü Seçimi").arg(NETA_APP_TITLE, NETA_APP_VERSION));
    setFixedSize(980, 680);
    setStyleSheet("background-color: #11141A; color: #E0E6ED; font-family: 'Segoe UI', Arial, sans-serif;");

    // Varsayılan rol
    m_selectedRole = AppConfig::instance().role();

    setupUi();
    updateCardStyles();
    checkDatabaseStatus();
}

void ModeSelectDialog::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(32, 26, 32, 24);

    // --- 1. ÜST BAŞLIK & LOGO ALANI ---
    QHBoxLayout *headerLayout = new QHBoxLayout();
    QLabel *lblLogo = new QLabel(this);
    QPixmap pix(":/resources/images/neta_app.png");
    if (pix.isNull()) {
        pix.load(":/resources/images/logo.png");
    }
    if (!pix.isNull()) {
        lblLogo->setPixmap(pix.scaledToHeight(48, Qt::SmoothTransformation));
    } else {
        lblLogo->setText("<h2 style='color:#64B5F6; margin:0;'>NETA</h2>");
    }

    QVBoxLayout *titleTextLayout = new QVBoxLayout();
    titleTextLayout->setSpacing(3);
    QLabel *lblMainTitle = new QLabel(QString("%1 v%2").arg(NETA_APP_NAME, NETA_APP_VERSION), this);
    lblMainTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #FFFFFF;");
    
    QLabel *lblSubtitle = new QLabel("Lütfen bu terminalin fabrika yerel ağındaki (LAN) çalışma rolünü seçiniz:", this);
    lblSubtitle->setStyleSheet("font-size: 13px; color: #9CA3AF;");

    titleTextLayout->addWidget(lblMainTitle);
    titleTextLayout->addWidget(lblSubtitle);

    headerLayout->addWidget(lblLogo);
    headerLayout->addSpacing(18);
    headerLayout->addLayout(titleTextLayout);
    headerLayout->addStretch();

    QPushButton *btnAbout = new QPushButton("ℹ️ Hakkında", this);
    btnAbout->setFixedHeight(32);
    btnAbout->setCursor(Qt::PointingHandCursor);
    btnAbout->setStyleSheet(
        "QPushButton { background-color: #1E2530; color: #90CAF9; border: 1px solid #37474F; border-radius: 4px; padding: 4px 14px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: #263238; border-color: #64B5F6; color: #FFFFFF; }"
    );
    connect(btnAbout, &QPushButton::clicked, this, &ModeSelectDialog::onAboutClicked);
    headerLayout->addWidget(btnAbout);

    mainLayout->addLayout(headerLayout);

    // Ayırıcı ince çizgi
    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #242B35; background-color: #242B35; max-height: 1px;");
    mainLayout->addWidget(line);

    // --- 2. İKİ ADET ETKİLEŞİMLİ SEÇİM KARTI ---
    QHBoxLayout *cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(24);

    // >>> KART 1: TEST İSTASYONU (CLIENT) <<<
    m_cardClient = new QFrame(this);
    m_cardClient->setCursor(Qt::PointingHandCursor);
    m_cardClient->installEventFilter(this);
    QVBoxLayout *clientLayout = new QVBoxLayout(m_cardClient);
    clientLayout->setSpacing(14);
    clientLayout->setContentsMargins(20, 20, 20, 20);

    QHBoxLayout *clientTop = new QHBoxLayout();
    QLabel *lblClientIcon = new QLabel("🏭", m_cardClient);
    lblClientIcon->setStyleSheet("font-size: 36px;");
    
    QVBoxLayout *clientTitleVBox = new QVBoxLayout();
    QLabel *lblClientTitle = new QLabel("TEST MASASI", m_cardClient);
    lblClientTitle->setStyleSheet("font-size: 19px; font-weight: bold; color: #64B5F6;");
    QLabel *lblClientSub = new QLabel("Operatör Test Terminali", m_cardClient);
    lblClientSub->setStyleSheet("font-size: 12px; color: #9CA3AF;");
    clientTitleVBox->addWidget(lblClientTitle);
    clientTitleVBox->addWidget(lblClientSub);

    m_lblClientCheck = new QLabel("○ SEÇ", m_cardClient);
    m_lblClientCheck->setStyleSheet("font-size: 12px; font-weight: bold; padding: 4px 12px; border-radius: 10px; background-color: #263238; color: #90CAF9;");

    clientTop->addWidget(lblClientIcon);
    clientTop->addSpacing(10);
    clientTop->addLayout(clientTitleVBox);
    clientTop->addStretch();
    clientTop->addWidget(m_lblClientCheck);
    clientLayout->addLayout(clientTop);

    QLabel *lblClientDesc = new QLabel(
        "• Barkod okutarak DUT anten testini yürütür.<br>"
        "• 10 kanal otomatik RF telemetrisi toplar.<br>"
        "• Referans alıcısı ile anlık Delta dB hesaplar.", m_cardClient);
    lblClientDesc->setStyleSheet("color: #CFD8DC; font-size: 13px; line-height: 1.6;");
    clientLayout->addWidget(lblClientDesc);

    clientLayout->addStretch();

    // Kart İçi İstemci Parametreleri
    QFrame *clientSettingsBox = new QFrame(m_cardClient);
    clientSettingsBox->setStyleSheet("background-color: #161A22; border: 1px solid #232B36; border-radius: 8px; padding: 8px;");
    QGridLayout *gridClient = new QGridLayout(clientSettingsBox);
    gridClient->setSpacing(8);
    gridClient->setContentsMargins(10, 10, 10, 10);

    QLabel *lblMasa = new QLabel("Masa No:", clientSettingsBox);
    lblMasa->setStyleSheet("font-weight: bold; font-size: 12px;");
    gridClient->addWidget(lblMasa, 0, 0);

    m_comboStation = new QComboBox(clientSettingsBox);
    m_comboStation->setFixedHeight(34);
    m_comboStation->setStyleSheet("font-size: 12px;");
    m_comboStation->addItem("İstasyon 1 (Masa 1)", 1);
    m_comboStation->addItem("İstasyon 2 (Masa 2)", 2);
    m_comboStation->addItem("İstasyon 3 (Masa 3)", 3);
    int stIdx = m_comboStation->findData(AppConfig::instance().stationId());
    if (stIdx >= 0) m_comboStation->setCurrentIndex(stIdx);
    gridClient->addWidget(m_comboStation, 0, 1);

    QLabel *lblIp = new QLabel("Sunucu IP:", clientSettingsBox);
    lblIp->setStyleSheet("font-weight: bold; font-size: 12px;");
    gridClient->addWidget(lblIp, 1, 0);

    m_txtHostIp = new QLineEdit(clientSettingsBox);
    m_txtHostIp->setFixedHeight(34);
    m_txtHostIp->setStyleSheet("font-size: 12px;");
    m_txtHostIp->setPlaceholderText("Lobi ile otomatik bulunur...");
    m_txtHostIp->setText(AppConfig::instance().hostIp());
    gridClient->addWidget(m_txtHostIp, 1, 1);

    clientLayout->addWidget(clientSettingsBox);
    cardsLayout->addWidget(m_cardClient);

    // >>> KART 2: YÖNETİM & REFERANS SUNUCUSU (HOST) <<<
    m_cardHost = new QFrame(this);
    m_cardHost->setCursor(Qt::PointingHandCursor);
    m_cardHost->installEventFilter(this);
    QVBoxLayout *hostLayout = new QVBoxLayout(m_cardHost);
    hostLayout->setSpacing(14);
    hostLayout->setContentsMargins(20, 20, 20, 20);

    QHBoxLayout *hostTop = new QHBoxLayout();
    QLabel *lblHostIcon = new QLabel("🏢", m_cardHost);
    lblHostIcon->setStyleSheet("font-size: 36px;");

    QVBoxLayout *hostTitleVBox = new QVBoxLayout();
    QLabel *lblHostTitle = new QLabel("YÖNETİM & REFERANS", m_cardHost);
    lblHostTitle->setStyleSheet("font-size: 19px; font-weight: bold; color: #81C784;");
    QLabel *lblHostSub = new QLabel("Merkezi Sunucu & Ortak Alıcı", m_cardHost);
    lblHostSub->setStyleSheet("font-size: 12px; color: #9CA3AF;");
    hostTitleVBox->addWidget(lblHostTitle);
    hostTitleVBox->addWidget(lblHostSub);

    m_lblHostCheck = new QLabel("○ SEÇ", m_cardHost);
    m_lblHostCheck->setStyleSheet("font-size: 12px; font-weight: bold; padding: 4px 12px; border-radius: 10px; background-color: #263238; color: #A5D6A7;");

    hostTop->addWidget(lblHostIcon);
    hostTop->addSpacing(10);
    hostTop->addLayout(hostTitleVBox);
    hostTop->addStretch();
    hostTop->addWidget(m_lblHostCheck);
    hostLayout->addLayout(hostTop);

    QLabel *lblHostDesc = new QLabel(
        "• Sabit Golden Sample DRX9000 alıcısını yönetir.<br>"
        "• Masaların test kilitlerini sıraya sokar (Mutex).<br>"
        "• Merkezi MySQL ve resmi Excel raporları üretir.", m_cardHost);
    lblHostDesc->setStyleSheet("color: #CFD8DC; font-size: 13px; line-height: 1.6;");
    hostLayout->addWidget(lblHostDesc);

    hostLayout->addStretch();

    // Kart İçi Sunucu Bilgi Rozetleri
    QFrame *hostInfoBox = new QFrame(m_cardHost);
    hostInfoBox->setStyleSheet("background-color: #161A22; border: 1px solid #232B36; border-radius: 8px; padding: 8px;");
    QVBoxLayout *vboxHostInfo = new QVBoxLayout(hostInfoBox);
    vboxHostInfo->setSpacing(6);
    vboxHostInfo->setContentsMargins(10, 10, 10, 10);

    QLabel *lblH1 = new QLabel("📡 TCP Port: <b>9100</b> | Lobi: UDP <b>9101</b>", hostInfoBox);
    lblH1->setStyleSheet("color: #81C784; font-size: 12px;");
    QLabel *lblH2 = new QLabel("🗄️ MySQL Port: <b>3306</b> (Ağa Açık)", hostInfoBox);
    lblH2->setStyleSheet("color: #90CAF9; font-size: 12px;");
    QLabel *lblH3 = new QLabel("🔒 Otomatik Emniyet Kilidi & FIFO Sıralama", hostInfoBox);
    lblH3->setStyleSheet("color: #FFB74D; font-size: 12px;");

    vboxHostInfo->addWidget(lblH1);
    vboxHostInfo->addWidget(lblH2);
    vboxHostInfo->addWidget(lblH3);

    m_btnInstallDb = new QPushButton(hostInfoBox);
    m_btnInstallDb->setFixedHeight(34);
    m_btnInstallDb->setStyleSheet("font-size: 12px; font-weight: bold;");
    connect(m_btnInstallDb, &QPushButton::clicked, this, &ModeSelectDialog::onInstallDbClicked);
    vboxHostInfo->addWidget(m_btnInstallDb);

    hostLayout->addWidget(hostInfoBox);

    cardsLayout->addWidget(m_cardHost);
    mainLayout->addLayout(cardsLayout);

    // --- 3. ALT SEÇENEKLER & EYLEM BUTONLARI ---
    QHBoxLayout *botLayout = new QHBoxLayout();
    m_chkRemember = new QCheckBox("Bu bilgisayar için seçimi hatırla (Bir sonraki açılışta doğrudan başlat)", this);
    m_chkRemember->setStyleSheet("color: #B0BEC5; font-size: 13px;");
    botLayout->addWidget(m_chkRemember);
    botLayout->addStretch();

    QLabel *lblDev = new QLabel(QString("Geliştirici: <b>%1</b> | %2").arg(NETA_APP_DEVELOPER, NETA_APP_ORGANIZATION), this);
    lblDev->setStyleSheet("color: #78909C; font-size: 12px;");
    botLayout->addWidget(lblDev);

    mainLayout->addLayout(botLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_btnExit = new QPushButton("Çıkış", this);
    m_btnExit->setFixedSize(130, 44);
    m_btnExit->setStyleSheet("background-color: #2D3748; color: #E2E8F0; font-size: 13px; font-weight: bold; border-radius: 6px;");
    connect(m_btnExit, &QPushButton::clicked, this, &QDialog::reject);

    m_btnLaunch = new QPushButton("Sistemi Başlat  ➔", this);
    m_btnLaunch->setFixedHeight(44);
    m_btnLaunch->setMinimumWidth(250);
    m_btnLaunch->setStyleSheet("background-color: #1976D2; color: #FFFFFF; font-size: 15px; font-weight: bold; border-radius: 6px; padding: 0 28px;");
    connect(m_btnLaunch, &QPushButton::clicked, this, &ModeSelectDialog::onAccept);

    btnLayout->addWidget(m_btnExit);
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnLaunch);
    mainLayout->addLayout(btnLayout);
}

bool ModeSelectDialog::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (watched == m_cardClient) {
            selectRole(AppRole::Client);
            return true;
        } else if (watched == m_cardHost) {
            selectRole(AppRole::Host);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ModeSelectDialog::selectRole(AppRole role) {
    m_selectedRole = role;
    updateCardStyles();
}

void ModeSelectDialog::updateCardStyles() {
    if (m_selectedRole == AppRole::Client) {
        // İstemci Seçili
        m_cardClient->setStyleSheet(
            "QFrame { background-color: #182232; border: 2px solid #2196F3; border-radius: 10px; }"
        );
        m_lblClientCheck->setText("✔ SEÇİLDİ");
        m_lblClientCheck->setStyleSheet("font-size: 12px; font-weight: bold; padding: 4px 12px; border-radius: 10px; background-color: #1976D2; color: #FFFFFF;");

        m_cardHost->setStyleSheet(
            "QFrame { background-color: #181C24; border: 1px solid #2C3545; border-radius: 10px; }"
            "QFrame:hover { border: 1px solid #455A64; background-color: #1C222C; }"
        );
        m_lblHostCheck->setText("○ SEÇ");
        m_lblHostCheck->setStyleSheet("font-size: 12px; font-weight: bold; padding: 4px 12px; border-radius: 10px; background-color: #242B35; color: #78909C;");

        m_comboStation->setEnabled(true);
        m_txtHostIp->setEnabled(true);

        m_btnLaunch->setText("Test Masasını Başlat  ➔");
        m_btnLaunch->setStyleSheet("background-color: #1976D2; color: #FFFFFF; font-size: 15px; font-weight: bold; border-radius: 6px; padding: 0 28px;");
    } else {
        // Host Seçili
        m_cardClient->setStyleSheet(
            "QFrame { background-color: #181C24; border: 1px solid #2C3545; border-radius: 10px; }"
            "QFrame:hover { border: 1px solid #455A64; background-color: #1C222C; }"
        );
        m_lblClientCheck->setText("○ SEÇ");
        m_lblClientCheck->setStyleSheet("font-size: 12px; font-weight: bold; padding: 4px 12px; border-radius: 10px; background-color: #242B35; color: #78909C;");

        m_cardHost->setStyleSheet(
            "QFrame { background-color: #142820; border: 2px solid #2E7D32; border-radius: 10px; }"
        );
        m_lblHostCheck->setText("✔ SEÇİLDİ");
        m_lblHostCheck->setStyleSheet("font-size: 12px; font-weight: bold; padding: 4px 12px; border-radius: 10px; background-color: #2E7D32; color: #FFFFFF;");

        m_comboStation->setEnabled(false);
        m_txtHostIp->setEnabled(false);

        m_btnLaunch->setText("Yönetim Sunucusunu Başlat  ➔");
        m_btnLaunch->setStyleSheet("background-color: #2E7D32; color: #FFFFFF; font-size: 15px; font-weight: bold; border-radius: 6px; padding: 0 28px;");
    }
}

int ModeSelectDialog::selectedStationId() const {
    return m_comboStation->currentData().toInt();
}

QString ModeSelectDialog::serverIp() const {
    return m_txtHostIp->text().trimmed();
}

bool ModeSelectDialog::rememberSelection() const {
    return m_chkRemember->isChecked();
}

void ModeSelectDialog::checkDatabaseStatus() {
    if (!m_btnInstallDb) return;
    if (DependencyInstaller::isMySqlListening()) {
        m_btnInstallDb->setText("✔ MySQL Servisi Aktif (Port 3306)");
        m_btnInstallDb->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; font-weight: bold; border-radius: 4px; font-size: 11px;");
    } else {
        m_btnInstallDb->setText("⚡ Tek Tıkla MySQL & DB Kur");
        m_btnInstallDb->setStyleSheet("background-color: #E65100; color: #FFFFFF; font-weight: bold; border-radius: 4px; font-size: 11px;");
    }
}

void ModeSelectDialog::onInstallDbClicked() {
    DependencyInstaller::autoSetupHostEnvironment(this);
    checkDatabaseStatus();
}

void ModeSelectDialog::onAccept() {
    if (m_selectedRole == AppRole::Host) {
        // Kendisi yönetici (Host) olarak seçildi!
        // Eğer MySQL yok ise otomatik olarak kur!
        if (!DependencyInstaller::isMySqlListening("127.0.0.1", 3306)) {
            auto reply = QMessageBox::question(
                this,
                "MySQL Servisi Eksik",
                "Bu bilgisayar Yönetim Sunucusu (Host) olarak seçildi.\n"
                "Merkezi veritabanı (MariaDB/MySQL) şu anda bu bilgisayarda çalışmıyor.\n\n"
                "Gerekli tüm servisler, ağ portları ve fabrika test tabloları şimdi otomatik olarak kurulsun mu?",
                QMessageBox::Yes | QMessageBox::No
            );

            if (reply != QMessageBox::Yes) {
                return;
            }

            bool ok = DependencyInstaller::autoSetupHostEnvironment(this);
            if (!ok && !DependencyInstaller::isMySqlListening("127.0.0.1", 3306)) {
                QMessageBox::warning(this, "Hata", "Veritabanı kurulumu tamamlanamadı.\nLütfen yönetici yetkisi verildiğinden emin olun.");
                return;
            }
        } else {
            // MySQL çalışıyor, varsayılan değerler eksikse onları da kur!
            AppConfig &cfg = AppConfig::instance();
            DbManager::instance().connectToDatabase(cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword());
            DbManager::instance().ensureDefaultDataIfMissing();
        }
    }
    accept();
}

void ModeSelectDialog::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}
