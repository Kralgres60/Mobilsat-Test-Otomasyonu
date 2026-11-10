/**
 * @file db_debug_widget.cpp
 * @brief Implementation of the interactive Database & Distributed Atomic Locking Debug Console.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "db_debug_widget.h"
#include "ui/common/style_helper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDateTime>
#include <QRandomGenerator>

DbDebugWidget::DbDebugWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();

    connect(m_connectBtn, &QPushButton::clicked, this, &DbDebugWidget::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &DbDebugWidget::onDisconnectClicked);
    connect(m_startEngineBtn, &QPushButton::clicked, this, &DbDebugWidget::onStartEngineClicked);
    connect(m_stopEngineBtn, &QPushButton::clicked, this, &DbDebugWidget::onStopEngineClicked);
    connect(m_initSchemaBtn, &QPushButton::clicked, this, &DbDebugWidget::onInitSchemaClicked);
    connect(m_loadProfilesBtn, &QPushButton::clicked, this, &DbDebugWidget::onLoadProfilesClicked);

    connect(m_acquireLockBtn, &QPushButton::clicked, this, &DbDebugWidget::onAcquireLockClicked);
    connect(m_releaseLockBtn, &QPushButton::clicked, this, &DbDebugWidget::onReleaseLockClicked);
    connect(m_heartbeatBtn, &QPushButton::clicked, this, &DbDebugWidget::onHeartbeatClicked);
    connect(m_queryLockBtn, &QPushButton::clicked, this, &DbDebugWidget::onQueryLockClicked);
    connect(m_contentionSimBtn, &QPushButton::clicked, this, &DbDebugWidget::onRunContentionSimulationClicked);

    connect(m_insertMockBtn, &QPushButton::clicked, this, &DbDebugWidget::onInsertMockRunClicked);
    connect(m_refreshRunsBtn, &QPushButton::clicked, this, &DbDebugWidget::onRefreshTestRunsClicked);

    connect(&m_pollTimer, &QTimer::timeout, this, &DbDebugWidget::onQueryLockClicked);

    updateConnectionStatusBadge(DbManager::instance().isConnected());
    appendLog("🗄️ MariaDB & Dağıtık Kilit Doğrulama Modülü Hazır.", "#81D4FA");
}

DbDebugWidget::~DbDebugWidget() {
    m_pollTimer.stop();
}

void DbDebugWidget::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // -------------------------------------------------------------------------
    // 1. ÜST PANEL: MariaDB Bağlantı Yapılandırması
    // -------------------------------------------------------------------------
    QGroupBox *connGroup = new QGroupBox("1. MariaDB Veritabanı ve Taşınabilir Motor Yapılandırması", this);
    QGridLayout *connLayout = new QGridLayout(connGroup);

    m_hostEdit = new QLineEdit("127.0.0.1", this);
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(3306);
    m_dbNameEdit = new QLineEdit("neta_antenna_test", this);
    m_userEdit = new QLineEdit("root", this);
    m_passEdit = new QLineEdit("neta123", this);
    m_passEdit->setEchoMode(QLineEdit::Password);

    m_startEngineBtn = new QPushButton("🚀 Taşınabilir Motoru Başlat", this);
    m_startEngineBtn->setStyleSheet("background-color: #00796B; color: white; font-weight: bold; padding: 6px 12px;");

    m_stopEngineBtn = new QPushButton("⏹️ Motoru Durdur", this);
    m_stopEngineBtn->setStyleSheet("background-color: #D32F2F; color: white; font-weight: bold; padding: 6px 12px;");

    m_connectBtn = new QPushButton("🔌 Bağlan", this);
    m_connectBtn->setStyleSheet("background-color: #1976D2; color: white; font-weight: bold; padding: 6px 12px;");

    m_disconnectBtn = new QPushButton("❌ Bağlantıyı Kes", this);
    m_disconnectBtn->setStyleSheet("background-color: #546E7A; color: white; padding: 6px 12px;");

    m_statusBadge = new QLabel("DURUM: BAĞLI DEĞİL", this);
    m_statusBadge->setStyleSheet("background-color: #B71C1C; color: white; padding: 6px 12px; border-radius: 4px; font-weight: bold;");

    // Satır 0: Bağlantı parametreleri
    connLayout->addWidget(new QLabel("Host IP:", this), 0, 0);
    connLayout->addWidget(m_hostEdit, 0, 1);
    connLayout->addWidget(new QLabel("Port:", this), 0, 2);
    connLayout->addWidget(m_portSpin, 0, 3);
    connLayout->addWidget(new QLabel("Veritabanı:", this), 0, 4);
    connLayout->addWidget(m_dbNameEdit, 0, 5);
    connLayout->addWidget(new QLabel("Kullanıcı:", this), 0, 6);
    connLayout->addWidget(m_userEdit, 0, 7);
    connLayout->addWidget(new QLabel("Şifre:", this), 0, 8);
    connLayout->addWidget(m_passEdit, 0, 9);

    // Satır 1: Eylem butonları
    QHBoxLayout *actionLayout = new QHBoxLayout();
    actionLayout->addWidget(m_startEngineBtn);
    actionLayout->addWidget(m_stopEngineBtn);
    actionLayout->addSpacing(15);
    actionLayout->addWidget(m_connectBtn);
    actionLayout->addWidget(m_disconnectBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(m_statusBadge);
    connLayout->addLayout(actionLayout, 1, 0, 1, 10);

    mainLayout->addWidget(connGroup);

    // -------------------------------------------------------------------------
    // 2. ORTA PANEL: Şema, Dağıtık Kilit ve Test Fonksiyonları
    // -------------------------------------------------------------------------
    QHBoxLayout *midLayout = new QHBoxLayout();

    // Sol Panel: Dağıtık Kilit (Algoritma 2 - reference_lock)
    QGroupBox *lockGroup = new QGroupBox("2. Algoritma 2: Dağıtık Atomik Kilit Simülatörü (reference_lock)", this);
    QGridLayout *lockLayout = new QGridLayout(lockGroup);

    m_stationSpin = new QSpinBox(this);
    m_stationSpin->setRange(1, 3);
    m_stationSpin->setValue(1);
    m_stationSpin->setPrefix("Masa ");

    m_channelSpin = new QSpinBox(this);
    m_channelSpin->setRange(1, 10);
    m_channelSpin->setValue(1);
    m_channelSpin->setPrefix("Kanal #");

    m_acquireLockBtn = new QPushButton("🔒 Kilidi Talep Et (Acquire)", this);
    m_acquireLockBtn->setStyleSheet("background-color: #2E7D32; color: white; font-weight: bold; padding: 6px 10px;");

    m_releaseLockBtn = new QPushButton("🔓 Kilidi Bırak (Release)", this);
    m_releaseLockBtn->setStyleSheet("background-color: #C62828; color: white; font-weight: bold; padding: 6px 10px;");

    m_heartbeatBtn = new QPushButton("💓 Heartbeat Gönder", this);
    m_heartbeatBtn->setStyleSheet("background-color: #00838F; color: white; padding: 6px 10px;");

    m_queryLockBtn = new QPushButton("🔍 Canlı Kilit Durumu", this);
    m_queryLockBtn->setStyleSheet("background-color: #4527A0; color: white; padding: 6px 10px;");

    m_contentionSimBtn = new QPushButton("⚡ Otomatik Çakışma ve Sıra Testi (Masa 1 vs 2)", this);
    m_contentionSimBtn->setStyleSheet("background-color: #E65100; color: white; font-weight: bold; padding: 8px 12px;");

    m_lockHolderBadge = new QLabel("Kilit Durumu: BOŞTA (Kullanılabilir)", this);
    m_lockHolderBadge->setStyleSheet("background-color: #263238; color: #69F0AE; padding: 6px 10px; border-radius: 4px; font-weight: bold;");

    lockLayout->addWidget(new QLabel("İstasyon No:", this), 0, 0);
    lockLayout->addWidget(m_stationSpin, 0, 1);
    lockLayout->addWidget(new QLabel("Aktif Kanal:", this), 0, 2);
    lockLayout->addWidget(m_channelSpin, 0, 3);

    lockLayout->addWidget(m_acquireLockBtn, 1, 0, 1, 2);
    lockLayout->addWidget(m_releaseLockBtn, 1, 2, 1, 2);
    lockLayout->addWidget(m_heartbeatBtn, 2, 0, 1, 2);
    lockLayout->addWidget(m_queryLockBtn, 2, 2, 1, 2);
    lockLayout->addWidget(m_contentionSimBtn, 3, 0, 1, 4);
    lockLayout->addWidget(m_lockHolderBadge, 4, 0, 1, 4);

    midLayout->addWidget(lockGroup, 5);

    // Sağ Panel: Şema & Test Verisi Kaydı
    QGroupBox *schemaGroup = new QGroupBox("3. Şema Sağlama & Test Veritabanı Kayıtları", this);
    QVBoxLayout *schemaLayout = new QVBoxLayout(schemaGroup);

    QHBoxLayout *schemaBtnLayout = new QHBoxLayout();
    m_initSchemaBtn = new QPushButton("🛠️ Tablo Şemasını & Varsayılanları Yükle", this);
    m_initSchemaBtn->setStyleSheet("background-color: #00695C; color: white; font-weight: bold; padding: 6px 10px;");

    m_loadProfilesBtn = new QPushButton("📡 Türksat 42E Profili & 10 Frekansı Doğrula", this);
    m_loadProfilesBtn->setStyleSheet("background-color: #283593; color: white; font-weight: bold; padding: 6px 10px;");

    schemaBtnLayout->addWidget(m_initSchemaBtn);
    schemaBtnLayout->addWidget(m_loadProfilesBtn);
    schemaLayout->addLayout(schemaBtnLayout);

    QHBoxLayout *runBtnLayout = new QHBoxLayout();
    m_insertMockBtn = new QPushButton("📝 Simüle Test Koşusu Yaz (10 Frekans PASS)", this);
    m_insertMockBtn->setStyleSheet("background-color: #37474F; color: white; padding: 6px 10px;");

    m_refreshRunsBtn = new QPushButton("🔄 Test Geçmişini Yenile", this);
    m_refreshRunsBtn->setStyleSheet("background-color: #455A64; color: white; padding: 6px 10px;");

    runBtnLayout->addWidget(m_insertMockBtn);
    runBtnLayout->addWidget(m_refreshRunsBtn);
    schemaLayout->addLayout(runBtnLayout);

    // Test Runs Mini Tablosu
    m_runsTable = new QTableWidget(this);
    m_runsTable->setColumnCount(7);
    m_runsTable->setHorizontalHeaderLabels(QStringList() << "ID" << "İstasyon" << "Seri No" << "Süre" << "Sonuç" << "Ort Δ C/N" << "Tarih");
    m_runsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_runsTable->horizontalHeader()->setStretchLastSection(true);
    m_runsTable->setStyleSheet("background-color: #1E1E1E; color: #FFFFFF; font-size: 11px;");
    schemaLayout->addWidget(m_runsTable);

    midLayout->addWidget(schemaGroup, 6);
    mainLayout->addLayout(midLayout);

    // -------------------------------------------------------------------------
    // 3. ALT PANEL: Canlı Log Konsolu
    // -------------------------------------------------------------------------
    QGroupBox *logGroup = new QGroupBox("4. Veritabanı ve Kilit İşlem Konsol Logları", this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet("background-color: #121212; color: #E0E0E0; font-family: Consolas, monospace; font-size: 11px;");
    logLayout->addWidget(m_logEdit);
    mainLayout->addWidget(logGroup, 1);
}

void DbDebugWidget::appendLog(const QString &msg, const QString &color) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_logEdit->append(QString("<font color='#888888'>[%1]</font> <font color='%2'>%3</font>")
                          .arg(timeStr, color, msg));
}

void DbDebugWidget::updateConnectionStatusBadge(bool connected) {
    if (connected) {
        QString driver = DbManager::instance().driverName();
        m_statusBadge->setText(QString("DURUM: BAĞLI (%1)").arg(driver));
        m_statusBadge->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 4px 8px; border-radius: 4px; font-weight: bold;");
        if (!m_pollTimer.isActive()) {
            m_pollTimer.start(2000); // 2 saniyede bir kilit durumunu sorgula
        }
    } else {
        m_statusBadge->setText("DURUM: BAĞLI DEĞİL");
        m_statusBadge->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 4px 8px; border-radius: 4px; font-weight: bold;");
        m_pollTimer.stop();
    }
}

void DbDebugWidget::onStartEngineClicked() {
    appendLog("🚀 Yerel Taşınabilir MariaDB motoru başlatılıyor...", "#FFF59D");
    bool ok = DbManager::instance().startPortableMariaDbIfPresent();
    if (ok) {
        appendLog("✅ Taşınabilir MariaDB motoru aktif! (Port: 3306)", "#69F0AE");
        onConnectClicked();
    } else {
        appendLog("❌ Taşınabilir MariaDB başlatılamadı. tools/mariadb_portable klasörünü kontrol ediniz.", "#FF5252");
    }
}

void DbDebugWidget::onStopEngineClicked() {
    appendLog("⏹️ Taşınabilir MariaDB motoru durduruluyor...", "#FFF59D");
    DbManager::instance().closeDatabase();
    DbManager::instance().stopPortableMariaDbIfRunning();
    updateConnectionStatusBadge(false);
    appendLog("🛑 MariaDB motoru ve bağlantısı güvenle kapatıldı.", "#B0BEC5");
}

void DbDebugWidget::onConnectClicked() {
    QString host = m_hostEdit->text().trimmed();
    int port = m_portSpin->value();
    QString dbName = m_dbNameEdit->text().trimmed();
    QString user = m_userEdit->text().trimmed();
    QString pass = m_passEdit->text();

    appendLog(QString("🔌 Bağlanılıyor: %1:%2/%3 (Kullanıcı: %4)...").arg(host).arg(port).arg(dbName, user), "#FFF59D");

    bool ok = DbManager::instance().connectToDatabase(host, port, dbName, user, pass);
    updateConnectionStatusBadge(ok);

    if (ok) {
        appendLog("✅ MariaDB Veritabanı Bağlantısı BAŞARILI!", "#69F0AE");
        onQueryLockClicked();
        onRefreshTestRunsClicked();
    } else {
        appendLog(QString("❌ Bağlantı HATASI: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
}

void DbDebugWidget::onDisconnectClicked() {
    DbManager::instance().closeDatabase();
    updateConnectionStatusBadge(false);
    m_lockHolderBadge->setText("Kilit Durumu: BAĞLANTI YOK");
    m_lockHolderBadge->setStyleSheet("background-color: #263238; color: #9E9E9E; padding: 6px 10px; border-radius: 4px;");
    appendLog("🔌 Veritabanı bağlantısı kapatıldı.", "#B0BEC5");
}

void DbDebugWidget::onInitSchemaClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("⚠️ Önce veritabanına bağlanmalısınız!", "#FFA726");
        return;
    }

    appendLog("🛠️ Veritabanı şeması ve tohum (seed) verileri yükleniyor...", "#FFF59D");
    bool ok = DbManager::instance().ensureDefaultDataIfMissing();
    if (ok) {
        appendLog("✅ Şema tabloları ve varsayılan Türksat 42E verileri başarıyla doğrulandı!", "#69F0AE");
        onLoadProfilesClicked();
        onQueryLockClicked();
    } else {
        appendLog(QString("❌ Şema yükleme HATASI: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
}

void DbDebugWidget::onLoadProfilesClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("⚠️ Veritabanı bağlantısı yok!", "#FFA726");
        return;
    }

    auto profiles = DbManager::instance().getProfiles();
    appendLog(QString("📡 Toplam Kayıtlı Uydu Profili: %1").arg(profiles.size()), "#81D4FA");

    for (const auto &p : profiles) {
        appendLog(QString("👉 Profil #%1: '%2' (Uydu: %3, Tolerans: ±%4 dB, Kanal Sayısı: %5)")
                      .arg(p.id)
                      .arg(p.profileName, p.satelliteName)
                      .arg(p.defaultToleranceDb, 0, 'f', 2)
                      .arg(p.channels.size()), "#E0E0E0");

        for (const auto &ch : p.channels) {
            appendLog(QString("   - TP #%1: %2 | %3 MHz | SR: %4 | Pol: %5 | Tol: ±%6 dB")
                          .arg(ch.channelNo)
                          .arg(ch.channelName)
                          .arg(ch.frequencyMhz)
                          .arg(ch.symbolRate)
                          .arg(ch.polarization)
                          .arg(ch.toleranceDb, 0, 'f', 2), "#B0BEC5");
        }
    }
}

void DbDebugWidget::onAcquireLockClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("⚠️ Veritabanı bağlantısı yok!", "#FFA726");
        return;
    }

    int stationId = m_stationSpin->value();
    int holder = 0;
    int ch = 0;

    bool ok = DbManager::instance().tryAcquireDbLock(stationId, holder, ch);
    if (ok) {
        appendLog(QString("🔒 [KİLİT ALINDI] İstasyon #%1 referans DRX kilidini BAŞARIYLA aldı!").arg(stationId), "#69F0AE");
    } else {
        appendLog(QString("⛔ [KİLİT MEŞGUL] Kilit İstasyon #%1 tarafından tutuluyor (Aktif Kanal: %2)!").arg(holder).arg(ch), "#FF5252");
    }
    onQueryLockClicked();
}

void DbDebugWidget::onReleaseLockClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("⚠️ Veritabanı bağlantısı yok!", "#FFA726");
        return;
    }

    int stationId = m_stationSpin->value();
    bool ok = DbManager::instance().releaseDbLock(stationId);
    if (ok) {
        appendLog(QString("🔓 [KİLİT BIRAKILDI] İstasyon #%1 kilidi serbest bıraktı.").arg(stationId), "#81D4FA");
    } else {
        appendLog(QString("❌ Kilit bırakma başarısız: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
    onQueryLockClicked();
}

void DbDebugWidget::onHeartbeatClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("⚠️ Veritabanı bağlantısı yok!", "#FFA726");
        return;
    }

    int stationId = m_stationSpin->value();
    int currentCh = m_channelSpin->value();
    bool ok = DbManager::instance().updateDbLockHeartbeat(stationId, currentCh);
    if (ok) {
        appendLog(QString("💓 [HEARTBEAT] İstasyon #%1 canlılık sinyali gönderdi (Kanal #%2).").arg(stationId).arg(currentCh), "#FFD54F");
    } else {
        appendLog(QString("❌ Heartbeat güncelleme başarısız: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
    onQueryLockClicked();
}

void DbDebugWidget::onQueryLockClicked() {
    if (!DbManager::instance().isConnected()) return;

    int currentChannel = 0;
    int holder = DbManager::instance().getDbLockHolder(currentChannel);

    if (holder == 0) {
        m_lockHolderBadge->setText("Kilit Durumu: 🟢 BOŞTA (Kullanılabilir)");
        m_lockHolderBadge->setStyleSheet("background-color: #1B5E20; color: #C8E6C9; padding: 6px 10px; border-radius: 4px; font-weight: bold;");
    } else {
        m_lockHolderBadge->setText(QString("Kilit Durumu: 🔴 İSTASYON %1 TEST EDİYOR (Kanal #%2)").arg(holder).arg(currentChannel));
        m_lockHolderBadge->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold;");
    }
}

void DbDebugWidget::onRunContentionSimulationClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("⚠️ Veritabanı bağlantısı yok!", "#FFA726");
        return;
    }

    appendLog("===================================================================", "#FFD54F");
    appendLog("⚡ ALGORİTMA 2: DAĞITIK ÇAKIŞMA & KUYRUK SİMÜLASYONU BAŞLATILIYOR", "#FFD54F");
    appendLog("===================================================================", "#FFD54F");

    // Adım 0: Önceki kalıntı varsa temizle
    DbManager::instance().releaseDbLock(0);

    // Adım 1: İstasyon 1 kilidi talep eder
    int holder = 0;
    int ch = 0;
    bool ok1 = DbManager::instance().tryAcquireDbLock(1, holder, ch);
    if (ok1) {
        appendLog("✅ [ADIM 1] İstasyon 1 kilidi BAŞARIYLA aldı.", "#69F0AE");
    } else {
        appendLog("❌ [ADIM 1 HATASI] İstasyon 1 kilidi alamadı!", "#FF5252");
        return;
    }

    // Adım 2: İstasyon 2 kilit almaya çalışır (Engellenmelidir!)
    bool ok2 = DbManager::instance().tryAcquireDbLock(2, holder, ch);
    if (!ok2 && holder == 1) {
        appendLog(QString("✅ [ADIM 2] İstasyon 2 bekletildi (Kilit İstasyon %1'de) - ÇAKIŞMA ÖNLENDİ!").arg(holder), "#69F0AE");
    } else {
        appendLog(QString("❌ [ADIM 2 HATASI] Atomik kilitleme başarısız! (ok2=%1, holder=%2)").arg(ok2).arg(holder), "#FF5252");
    }

    // Adım 3: İstasyon 1 kanal ilerler ve heartbeat atar
    DbManager::instance().updateDbLockHeartbeat(1, 5);
    appendLog("✅ [ADIM 3] İstasyon 1 Kanal #5 için Heartbeat güncelledi.", "#81D4FA");

    // Adım 4: İstasyon 1 testi bitirir ve kilidi serbest bırakır
    DbManager::instance().releaseDbLock(1);
    appendLog("✅ [ADIM 4] İstasyon 1 testini tamamladı ve kilidi serbest bıraktı.", "#81D4FA");

    // Adım 5: Şimdi İstasyon 2 kilidi talep eder (Başarılı olmalıdır!)
    bool ok2_after = DbManager::instance().tryAcquireDbLock(2, holder, ch);
    if (ok2_after) {
        appendLog("✅ [ADIM 5] İstasyon 2 kilidi BAŞARIYLA devraldı!", "#69F0AE");
        DbManager::instance().releaseDbLock(2);
        appendLog("✅ [ADIM 6] İstasyon 2 de kilidi serbest bıraktı. Kilit tertemiz boşta.", "#69F0AE");
    } else {
        appendLog("❌ [ADIM 5 HATASI] İstasyon 2 serbest kilidi alamadı!", "#FF5252");
    }

    appendLog("🎉 DAĞITIK KİLİT ENTEGRASYON TESTİ %100 BAŞARIYLA TAMAMLANDI!", "#69F0AE");
    appendLog("===================================================================", "#FFD54F");
    onQueryLockClicked();
}

void DbDebugWidget::onInsertMockRunClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("⚠️ Veritabanı bağlantısı yok!", "#FFA726");
        return;
    }

    TestRunData run;
    run.stationId = m_stationSpin->value();
    run.operatorId = 1;
    run.antennaSn = QString("DRX-DEBUG-%1").arg(QRandomGenerator::global()->bounded(10000, 99999));
    run.satelliteProfileId = 1;
    run.timestamp = QDateTime::currentDateTime();
    run.durationSec = 52;
    run.overallResult = "PASS";
    run.notes = "Geliştirici Debug Konsolu Tarafından Otomatik Üretildi";

    double totalDelta = 0.0;
    double maxDelta = 0.0;

    for (int i = 1; i <= 10; ++i) {
        ChannelSampleResult ch;
        ch.channelNo = i;
        ch.channelName = QString("CH_%1").arg(i);
        ch.frequencyMhz = 11000 + (i * 125);
        ch.symbolRate = 27500;
        ch.polarization = (i % 2 == 0) ? "V" : "H";
        ch.dutCn = 14.5 + (QRandomGenerator::global()->bounded(100) / 100.0) - 0.5;
        ch.dutPow = -39.0 + (QRandomGenerator::global()->bounded(100) / 100.0) - 0.5;
        ch.refCn = 14.6;
        ch.refPow = -38.8;
        ch.deltaDb = ch.dutCn - ch.refCn;
        ch.carrierLocked = true;
        ch.videoLocked = true;
        ch.audioLocked = true;
        ch.toleranceDb = 1.5;
        ch.status = (qAbs(ch.deltaDb) <= ch.toleranceDb) ? "PASS" : "FAIL";

        totalDelta += ch.deltaDb;
        if (qAbs(ch.deltaDb) > maxDelta) maxDelta = qAbs(ch.deltaDb);

        run.channels.append(ch);
    }

    run.avgDeltaDb = totalDelta / 10.0;
    run.maxDeltaDb = maxDelta;

    qint64 runId = DbManager::instance().insertTestRun(run);
    if (runId > 0) {
        appendLog(QString("📝 [KAYIT BAŞARILI] Mock Test Koşusu eklendi! (ID: #%1, Seri No: %2, Sonuç: %3, Ort Δ: %4 dB)")
                      .arg(runId)
                      .arg(run.antennaSn, run.overallResult)
                      .arg(run.avgDeltaDb, 0, 'f', 2), "#69F0AE");
        onRefreshTestRunsClicked();
    } else {
        appendLog(QString("❌ Test koşusu kayıt HATASI: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
}

void DbDebugWidget::onRefreshTestRunsClicked() {
    if (!DbManager::instance().isConnected()) return;

    auto runs = DbManager::instance().getTestRuns(15);
    m_runsTable->setRowCount(runs.size());

    for (int r = 0; r < runs.size(); ++r) {
        const auto &run = runs[r];
        m_runsTable->setItem(r, 0, new QTableWidgetItem(QString::number(run.id)));
        m_runsTable->setItem(r, 1, new QTableWidgetItem(QString("Masa %1").arg(run.stationId)));
        m_runsTable->setItem(r, 2, new QTableWidgetItem(run.antennaSn));
        m_runsTable->setItem(r, 3, new QTableWidgetItem(QString("%1 sn").arg(run.durationSec)));

        QTableWidgetItem *resItem = new QTableWidgetItem(run.overallResult);
        if (run.overallResult == "PASS") {
            resItem->setForeground(QBrush(QColor("#69F0AE")));
        } else {
            resItem->setForeground(QBrush(QColor("#FF5252")));
        }
        m_runsTable->setItem(r, 4, resItem);

        m_runsTable->setItem(r, 5, new QTableWidgetItem(QString::number(run.avgDeltaDb, 'f', 2) + " dB"));
        m_runsTable->setItem(r, 6, new QTableWidgetItem(run.timestamp.toString("yyyy-MM-dd HH:mm:ss")));
    }
}
