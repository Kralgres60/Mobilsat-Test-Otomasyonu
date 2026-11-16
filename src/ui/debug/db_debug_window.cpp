/**
 * @file db_debug_window.cpp
 * @brief Implementation of standalone dedicated MariaDB and locking test tool.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "db_debug_window.h"
#include "database/db_manager.h"
#include "core/app_config.h"
#include "ui/common/style_helper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QDateTime>
#include <QProcess>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>

DbDebugWindow::DbDebugWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUi();

    connect(&m_lockPollTimer, &QTimer::timeout, this, &DbDebugWindow::onQueryLockClicked);
    m_lockPollTimer.start(2500);

    onQueryLockClicked();
    onRefreshTablesClicked();
    appendLog("🗄️ Veritabanı ve Kilit Yönetim Aracı hazır.", "#34D399");
}

DbDebugWindow::~DbDebugWindow() {
    m_lockPollTimer.stop();
}

void DbDebugWindow::setupUi() {
    setWindowTitle("NETA DRX9000 - Veritabanı ve Kilit Test Aracı");
    resize(920, 680);
    setStyleSheet("background-color: #1E1E2E; color: #E0E0E0; font-family: 'Segoe UI', Arial, sans-serif;");

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // =========================================================================
    // 1. Connection Card
    // =========================================================================
    QGroupBox *grpConn = new QGroupBox("📡 MariaDB / Veritabanı Bağlantısı", central);
    grpConn->setStyleSheet("QGroupBox { font-weight: bold; color: #89DCEB; border: 1px solid #45475A; "
                           "border-radius: 6px; margin-top: 6px; padding-top: 10px; } "
                           "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    QGridLayout *gridConn = new QGridLayout(grpConn);
    gridConn->setContentsMargins(10, 10, 10, 10);
    gridConn->setSpacing(8);

    const AppConfig &cfg = AppConfig::instance();

    m_txtHost = new QLineEdit(cfg.dbHost(), grpConn);
    m_txtHost->setPlaceholderText("Host IP (127.0.0.1)");

    m_spinPort = new QSpinBox(grpConn);
    m_spinPort->setRange(1, 65535);
    m_spinPort->setValue(cfg.dbPort());

    m_txtDbName = new QLineEdit(cfg.dbName(), grpConn);
    m_txtDbName->setPlaceholderText("Veritabanı Adı");

    m_txtUser = new QLineEdit(cfg.dbUser(), grpConn);
    m_txtUser->setPlaceholderText("Kullanıcı");

    m_txtPass = new QLineEdit(cfg.dbPassword(), grpConn);
    m_txtPass->setEchoMode(QLineEdit::Password);
    m_txtPass->setPlaceholderText("Şifre");

    m_btnConnect = new QPushButton("🔌 Bağlan / Yenile", grpConn);
    m_btnConnect->setStyleSheet(StyleHelper::getPastelBlueStyle());
    m_btnConnect->setCursor(Qt::PointingHandCursor);
    connect(m_btnConnect, &QPushButton::clicked, this, &DbDebugWindow::onConnectClicked);

    m_btnDisconnect = new QPushButton("Kes", grpConn);
    m_btnDisconnect->setStyleSheet(StyleHelper::getPastelRedStyle());
    m_btnDisconnect->setCursor(Qt::PointingHandCursor);
    connect(m_btnDisconnect, &QPushButton::clicked, this, &DbDebugWindow::onDisconnectClicked);

    m_btnStartEngine = new QPushButton("▶️ MariaDB Başlat", grpConn);
    m_btnStartEngine->setStyleSheet(StyleHelper::getPastelGreenStyle());
    m_btnStartEngine->setCursor(Qt::PointingHandCursor);
    connect(m_btnStartEngine, &QPushButton::clicked, this, &DbDebugWindow::onStartEngineClicked);

    m_btnStopEngine = new QPushButton("⏹️ Durdur", grpConn);
    m_btnStopEngine->setStyleSheet(StyleHelper::getPastelAmberStyle());
    m_btnStopEngine->setCursor(Qt::PointingHandCursor);
    connect(m_btnStopEngine, &QPushButton::clicked, this, &DbDebugWindow::onStopEngineClicked);

    m_lblConnStatus = new QLabel("Durum: Kontrol ediliyor...", grpConn);
    m_lblConnStatus->setStyleSheet("background-color: #313244; color: #CDD6F4; padding: 4px 10px; border-radius: 4px; font-weight: bold;");

    gridConn->addWidget(new QLabel("Host:", grpConn), 0, 0);
    gridConn->addWidget(m_txtHost, 0, 1);
    gridConn->addWidget(new QLabel("Port:", grpConn), 0, 2);
    gridConn->addWidget(m_spinPort, 0, 3);
    gridConn->addWidget(new QLabel("DB:", grpConn), 0, 4);
    gridConn->addWidget(m_txtDbName, 0, 5);

    gridConn->addWidget(new QLabel("Kullanıcı:", grpConn), 1, 0);
    gridConn->addWidget(m_txtUser, 1, 1);
    gridConn->addWidget(new QLabel("Şifre:", grpConn), 1, 2);
    gridConn->addWidget(m_txtPass, 1, 3);
    gridConn->addWidget(m_btnConnect, 1, 4);
    gridConn->addWidget(m_btnDisconnect, 1, 5);

    QHBoxLayout *hEngine = new QHBoxLayout();
    hEngine->addWidget(m_btnStartEngine);
    hEngine->addWidget(m_btnStopEngine);
    hEngine->addWidget(m_lblConnStatus, 1);
    gridConn->addLayout(hEngine, 2, 0, 1, 6);

    mainLayout->addWidget(grpConn);

    // =========================================================================
    // 2. Tabs: Tables & Schema vs Distributed Locking
    // =========================================================================
    QTabWidget *tabs = new QTabWidget(central);
    tabs->setStyleSheet("QTabWidget::pane { border: 1px solid #45475A; border-radius: 6px; background-color: #181825; } "
                        "QTabBar::tab { background-color: #313244; color: #CDD6F4; padding: 6px 14px; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; } "
                        "QTabBar::tab:selected { background-color: #45475A; color: #89DCEB; font-weight: bold; }");

    // -------------------------------------------------------------------------
    // Tab 1: Tables & Schema + Table Data Viewer
    // -------------------------------------------------------------------------
    QWidget *tabTables = new QWidget(tabs);
    QVBoxLayout *vTab1 = new QVBoxLayout(tabTables);
    vTab1->setContentsMargins(6, 6, 6, 6);
    vTab1->setSpacing(6);

    QSplitter *splitTables = new QSplitter(Qt::Horizontal, tabTables);
    splitTables->setHandleWidth(5);

    // Left Panel: Table List & Operations
    QWidget *wTablesLeft = new QWidget(splitTables);
    QVBoxLayout *vLeft = new QVBoxLayout(wTablesLeft);
    vLeft->setContentsMargins(0, 0, 4, 0);
    vLeft->setSpacing(6);

    QHBoxLayout *hTab1Btns1 = new QHBoxLayout();
    m_btnRefreshTables = new QPushButton("🔄 Tabloları Listele", wTablesLeft);
    m_btnRefreshTables->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnRefreshTables, &QPushButton::clicked, this, &DbDebugWindow::onRefreshTablesClicked);

    m_btnVerifySchema = new QPushButton("🛡️ Şemayı Doğrula", wTablesLeft);
    m_btnVerifySchema->setStyleSheet(StyleHelper::getPastelGreenStyle());
    connect(m_btnVerifySchema, &QPushButton::clicked, this, &DbDebugWindow::onVerifySchemaClicked);
    hTab1Btns1->addWidget(m_btnRefreshTables);
    hTab1Btns1->addWidget(m_btnVerifySchema);
    vLeft->addLayout(hTab1Btns1);

    QHBoxLayout *hTab1Btns2 = new QHBoxLayout();
    m_btnLoadProfiles = new QPushButton("🛰️ Standart Profiller", wTablesLeft);
    m_btnLoadProfiles->setStyleSheet(StyleHelper::getPastelAmberStyle());
    connect(m_btnLoadProfiles, &QPushButton::clicked, this, &DbDebugWindow::onLoadProfilesClicked);

    m_btnInsertMockRun = new QPushButton("➕ Örnek Test Kaydı", wTablesLeft);
    m_btnInsertMockRun->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnInsertMockRun, &QPushButton::clicked, this, &DbDebugWindow::onInsertMockRunClicked);
    hTab1Btns2->addWidget(m_btnLoadProfiles);
    hTab1Btns2->addWidget(m_btnInsertMockRun);
    vLeft->addLayout(hTab1Btns2);

    m_tableListWidget = new QTableWidget(wTablesLeft);
    m_tableListWidget->setColumnCount(2);
    m_tableListWidget->setHorizontalHeaderLabels({"Tablo Adı", "Kayıt (Rows)"});
    m_tableListWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tableListWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tableListWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableListWidget->setStyleSheet("QTableWidget { background-color: #11111B; border: 1px solid #313244; color: #CDD6F4; gridline-color: #313244; } "
                                    "QHeaderView::section { background-color: #1E1E2E; color: #89DCEB; font-weight: bold; padding: 4px; border: 1px solid #313244; }");
    connect(m_tableListWidget, &QTableWidget::cellClicked, this, &DbDebugWindow::onTableSelected);
    connect(m_tableListWidget, &QTableWidget::itemSelectionChanged, this, [this]() {
        int r = m_tableListWidget->currentRow();
        if (r >= 0) onTableSelected(r, 0);
    });
    vLeft->addWidget(m_tableListWidget);
    splitTables->addWidget(wTablesLeft);

    // Right Panel: Table Content Viewer
    QWidget *wTablesRight = new QWidget(splitTables);
    QVBoxLayout *vRight = new QVBoxLayout(wTablesRight);
    vRight->setContentsMargins(4, 0, 0, 0);
    vRight->setSpacing(6);

    QHBoxLayout *hDataTop = new QHBoxLayout();
    m_lblSelectedTable = new QLabel("📑 Tablo Verisi: Soldan bir tablo seçin", wTablesRight);
    m_lblSelectedTable->setStyleSheet("color: #89DCEB; font-weight: bold; font-size: 11pt;");

    m_spinRowLimit = new QSpinBox(wTablesRight);
    m_spinRowLimit->setRange(1, 10000);
    m_spinRowLimit->setValue(100);
    m_spinRowLimit->setSuffix(" kayıt");
    m_spinRowLimit->setFixedWidth(100);

    m_btnRefreshTableData = new QPushButton("🔄 Verileri Yenile", wTablesRight);
    m_btnRefreshTableData->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnRefreshTableData, &QPushButton::clicked, this, &DbDebugWindow::onRefreshTableDataClicked);

    hDataTop->addWidget(m_lblSelectedTable);
    hDataTop->addStretch();
    hDataTop->addWidget(new QLabel("Limit:", wTablesRight));
    hDataTop->addWidget(m_spinRowLimit);
    hDataTop->addWidget(m_btnRefreshTableData);
    vRight->addLayout(hDataTop);

    m_tableDataWidget = new QTableWidget(wTablesRight);
    m_tableDataWidget->setStyleSheet("QTableWidget { background-color: #0D0E15; border: 1px solid #313244; color: #E6F4EE; gridline-color: #252538; } "
                                     "QHeaderView::section { background-color: #181825; color: #34D399; font-weight: bold; padding: 4px; border: 1px solid #313244; }");
    m_tableDataWidget->setAlternatingRowColors(true);
    m_tableDataWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableDataWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableDataWidget->horizontalHeader()->setStretchLastSection(true);
    vRight->addWidget(m_tableDataWidget);
    splitTables->addWidget(wTablesRight);

    splitTables->setStretchFactor(0, 3);
    splitTables->setStretchFactor(1, 7);
    vTab1->addWidget(splitTables);

    tabs->addTab(tabTables, "📑 Veritabanı Tabloları ve Şema");

    // -------------------------------------------------------------------------
    // Tab 2: Distributed Locking (Algorithm 2)
    // -------------------------------------------------------------------------
    QWidget *tabLock = new QWidget(tabs);
    QVBoxLayout *vTab2 = new QVBoxLayout(tabLock);
    vTab2->setContentsMargins(12, 12, 12, 12);
    vTab2->setSpacing(10);

    m_lblLockStatus = new QLabel("Kilit Durumu: Sorgulanıyor...", tabLock);
    m_lblLockStatus->setStyleSheet("background-color: #181825; border: 2px solid #45475A; border-radius: 6px; padding: 12px; font-size: 13pt; font-weight: bold; color: #89DCEB;");
    vTab2->addWidget(m_lblLockStatus);

    QGroupBox *grpLockActions = new QGroupBox("🔒 Kilit Test ve Simülasyon Operasyonları", tabLock);
    grpLockActions->setStyleSheet("QGroupBox { font-weight: bold; color: #FAB387; border: 1px solid #45475A; border-radius: 6px; margin-top: 6px; padding: 10px; }");
    QGridLayout *gridLock = new QGridLayout(grpLockActions);
    gridLock->setContentsMargins(10, 10, 10, 10);
    gridLock->setSpacing(8);

    m_spinStationId = new QSpinBox(grpLockActions);
    m_spinStationId->setRange(1, 10);
    m_spinStationId->setValue(cfg.stationId());

    m_spinChannelNo = new QSpinBox(grpLockActions);
    m_spinChannelNo->setRange(1, 50);
    m_spinChannelNo->setValue(1);

    m_btnAcquireLock = new QPushButton("🔒 Kilit Al (Acquire)", grpLockActions);
    m_btnAcquireLock->setStyleSheet(StyleHelper::getPastelGreenStyle());
    connect(m_btnAcquireLock, &QPushButton::clicked, this, &DbDebugWindow::onAcquireLockClicked);

    m_btnReleaseLock = new QPushButton("🔓 Kilit Bırak (Release)", grpLockActions);
    m_btnReleaseLock->setStyleSheet(StyleHelper::getPastelAmberStyle());
    connect(m_btnReleaseLock, &QPushButton::clicked, this, &DbDebugWindow::onReleaseLockClicked);

    m_btnHeartbeat = new QPushButton("💓 Heartbeat Güncelle", grpLockActions);
    m_btnHeartbeat->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnHeartbeat, &QPushButton::clicked, this, &DbDebugWindow::onHeartbeatClicked);

    m_btnQueryLock = new QPushButton("🔍 Durumu Sorgula", grpLockActions);
    m_btnQueryLock->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnQueryLock, &QPushButton::clicked, this, &DbDebugWindow::onQueryLockClicked);

    gridLock->addWidget(new QLabel("Masa No (Station ID):", grpLockActions), 0, 0);
    gridLock->addWidget(m_spinStationId, 0, 1);
    gridLock->addWidget(new QLabel("Aktif Kanal No:", grpLockActions), 0, 2);
    gridLock->addWidget(m_spinChannelNo, 0, 3);
    gridLock->addWidget(m_btnAcquireLock, 1, 0, 1, 2);
    gridLock->addWidget(m_btnReleaseLock, 1, 2, 1, 2);
    gridLock->addWidget(m_btnHeartbeat, 2, 0, 1, 2);
    gridLock->addWidget(m_btnQueryLock, 2, 2, 1, 2);

    vTab2->addWidget(grpLockActions);
    vTab2->addStretch();

    tabs->addTab(tabLock, "⚖️ Algoritma 2 Dağıtık Kilit Hakemi");

    mainLayout->addWidget(tabs, 1);

    // =========================================================================
    // 3. Log Console
    // =========================================================================
    QHBoxLayout *hLogHeader = new QHBoxLayout();
    hLogHeader->addWidget(new QLabel("📋 İşlem Günlüğü (Console Logs):", central));
    hLogHeader->addStretch();
    m_btnClearLogs = new QPushButton("🧹 Temizle", central);
    m_btnClearLogs->setStyleSheet(StyleHelper::getPastelSlateStyle());
    connect(m_btnClearLogs, &QPushButton::clicked, this, &DbDebugWindow::onClearLogsClicked);
    hLogHeader->addWidget(m_btnClearLogs);
    mainLayout->addLayout(hLogHeader);

    m_txtLogs = new QTextEdit(central);
    m_txtLogs->setReadOnly(true);
    m_txtLogs->setFixedHeight(140);
    m_txtLogs->setStyleSheet("background-color: #11111B; border: 1px solid #313244; color: #A6ADC8; "
                            "font-family: 'Consolas', 'Courier New', monospace; font-size: 11px;");
    mainLayout->addWidget(m_txtLogs);

    updateConnectionBadge(DbManager::instance().isConnected());
}

void DbDebugWindow::appendLog(const QString &msg, const QString &color) {
    if (!m_txtLogs) return;
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_txtLogs->append(QString("<span style='color:#6C7086;'>[%1]</span> <span style='color:%2;'>%3</span>")
                          .arg(timeStr, color, msg.toHtmlEscaped()));
}

void DbDebugWindow::updateConnectionBadge(bool connected) {
    if (!m_lblConnStatus) return;
    if (connected) {
        QString drv = DbManager::instance().activeDriver();
        m_lblConnStatus->setText(QString("🟢 BAĞLI (%1 / %2:%3)")
                                     .arg(drv, m_txtHost->text(), m_spinPort->text()));
        m_lblConnStatus->setStyleSheet("background-color: #064E3B; border: 1px solid #10B981; color: #34D399; padding: 4px 10px; border-radius: 4px; font-weight: bold;");
    } else {
        m_lblConnStatus->setText("🔴 BAĞLANTI YOK");
        m_lblConnStatus->setStyleSheet("background-color: #450A0A; border: 1px solid #EF4444; color: #F87171; padding: 4px 10px; border-radius: 4px; font-weight: bold;");
    }
}

void DbDebugWindow::updateLockBadge(int lockedByStation, int channelNo) {
    if (!m_lblLockStatus) return;
    if (lockedByStation <= 0) {
        m_lblLockStatus->setText("🔓 Referans Cihazı BOŞTA (Kilit Müsait)");
        m_lblLockStatus->setStyleSheet("background-color: #064E3B; border: 2px solid #10B981; border-radius: 6px; padding: 12px; font-size: 13pt; font-weight: bold; color: #34D399;");
    } else {
        m_lblLockStatus->setText(QString("🔒 KİLİTLİ: Masa %1 tarafından kullanımda (Kanal: %2)")
                                     .arg(lockedByStation).arg(channelNo));
        m_lblLockStatus->setStyleSheet("background-color: #451A03; border: 2px solid #F59E0B; border-radius: 6px; padding: 12px; font-size: 13pt; font-weight: bold; color: #FBBF24;");
    }
}

void DbDebugWindow::onConnectClicked() {
    QString host = m_txtHost->text().trimmed();
    int port = m_spinPort->value();
    QString db = m_txtDbName->text().trimmed();
    QString user = m_txtUser->text().trimmed();
    QString pass = m_txtPass->text();

    appendLog(QString("Veritabanına bağlanılıyor: %1@%2:%3/%4").arg(user, host).arg(port).arg(db), "#89DCEB");
    bool ok = DbManager::instance().connectToDatabase(host, port, db, user, pass);
    updateConnectionBadge(ok);

    if (ok) {
        appendLog("✔ Veritabanı bağlantısı başarıyla kuruldu!", "#34D399");
        onRefreshTablesClicked();
        onQueryLockClicked();
    } else {
        appendLog(QString("❌ Bağlantı hatası: %1").arg(DbManager::instance().lastError()), "#EF4444");
    }
}

void DbDebugWindow::onDisconnectClicked() {
    DbManager::instance().closeDatabase();
    updateConnectionBadge(false);
    appendLog("Veritabanı bağlantısı kapatıldı.", "#FAB387");
}

void DbDebugWindow::onRefreshTablesClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("Tablolar listelenemedi: Veritabanı bağlı değil.", "#EF4444");
        return;
    }

    m_tableListWidget->setRowCount(0);
    QStringList tables = {
        "satellite_profiles", "profile_channels", "work_orders",
        "test_runs", "test_run_channels", "golden_frequencies",
        "system_settings", "audit_logs", "reference_lock", "users"
    };

    QSqlDatabase db = QSqlDatabase::database(DbManager::instance().connectionName());
    int row = 0;
    int selectedIdx = -1;
    for (const QString &tbl : tables) {
        m_tableListWidget->insertRow(row);
        m_tableListWidget->setItem(row, 0, new QTableWidgetItem(tbl));

        QSqlQuery q(db);
        q.prepare(QString("SELECT COUNT(*) FROM `%1`").arg(tbl));
        if (q.exec() && q.next()) {
            int cnt = q.value(0).toInt();
            auto *itCnt = new QTableWidgetItem(QString::number(cnt));
            itCnt->setTextAlignment(Qt::AlignCenter);
            m_tableListWidget->setItem(row, 1, itCnt);
        } else {
            auto *itErr = new QTableWidgetItem("Hata");
            itErr->setTextAlignment(Qt::AlignCenter);
            itErr->setForeground(QBrush(QColor("#EF4444")));
            m_tableListWidget->setItem(row, 1, itErr);
        }

        if (tbl == m_currentSelectedTable) {
            selectedIdx = row;
        }
        row++;
    }

    appendLog("Tablo listesi ve kayıt sayıları güncellendi.", "#89DCEB");

    // Re-select active table or first table
    if (selectedIdx >= 0) {
        m_tableListWidget->selectRow(selectedIdx);
    } else if (m_tableListWidget->rowCount() > 0) {
        m_tableListWidget->selectRow(0);
    }
}

void DbDebugWindow::onTableSelected(int row, int col) {
    Q_UNUSED(col);
    if (row < 0 || row >= m_tableListWidget->rowCount()) return;
    QTableWidgetItem *item = m_tableListWidget->item(row, 0);
    if (!item) return;

    QString tableName = item->text().trimmed();
    m_currentSelectedTable = tableName;
    loadTableData(tableName);
}

void DbDebugWindow::onRefreshTableDataClicked() {
    if (m_currentSelectedTable.isEmpty()) {
        int r = m_tableListWidget->currentRow();
        if (r >= 0 && r < m_tableListWidget->rowCount()) {
            QTableWidgetItem *item = m_tableListWidget->item(r, 0);
            if (item) m_currentSelectedTable = item->text().trimmed();
        }
    }
    if (!m_currentSelectedTable.isEmpty()) {
        loadTableData(m_currentSelectedTable);
    }
}

void DbDebugWindow::loadTableData(const QString &tableName) {
    if (!DbManager::instance().isConnected()) {
        appendLog("Tablo verisi yüklenemedi: Veritabanı bağlı değil.", "#EF4444");
        return;
    }
    if (tableName.isEmpty()) return;

    QSqlDatabase db = QSqlDatabase::database(DbManager::instance().connectionName());
    int limit = m_spinRowLimit ? m_spinRowLimit->value() : 100;

    QSqlQuery q(db);
    QString sql = QString("SELECT * FROM `%1` LIMIT %2").arg(tableName).arg(limit);
    if (!q.exec(sql)) {
        appendLog(QString("❌ Tablo sorgu hatası (%1): %2").arg(tableName, q.lastError().text()), "#EF4444");
        m_lblSelectedTable->setText(QString("❌ Hata: %1 tablosu okunamadı").arg(tableName));
        return;
    }

    QSqlRecord rec = q.record();
    int colCount = rec.count();

    m_tableDataWidget->clear();
    m_tableDataWidget->setColumnCount(colCount);
    m_tableDataWidget->setRowCount(0);

    QStringList headers;
    for (int c = 0; c < colCount; ++c) {
        headers << rec.fieldName(c);
    }
    m_tableDataWidget->setHorizontalHeaderLabels(headers);

    int rowIdx = 0;
    while (q.next()) {
        m_tableDataWidget->insertRow(rowIdx);
        for (int c = 0; c < colCount; ++c) {
            QString valStr = q.value(c).toString();
            QTableWidgetItem *cell = new QTableWidgetItem(valStr);
            m_tableDataWidget->setItem(rowIdx, c, cell);
        }
        rowIdx++;
    }

    m_tableDataWidget->resizeColumnsToContents();
    m_lblSelectedTable->setText(QString("📑 Tablo: %1 (%2 kayıt listelendi)").arg(tableName).arg(rowIdx));
    appendLog(QString("✔ '%1' tablosundan %2 satır veri listelendi.").arg(tableName).arg(rowIdx), "#34D399");
}

void DbDebugWindow::onVerifySchemaClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("Şema doğrulanamadı: Veritabanı bağlı değil.", "#EF4444");
        return;
    }

    appendLog("Şema doğrulanıyor ve eksik tablolar kontrol ediliyor...", "#89DCEB");
    bool ok = DbManager::instance().ensureDefaultDataIfMissing();
    if (ok) {
        appendLog("✔ Şema doğrulandı ve eksik tablolar garanti altına alındı.", "#34D399");
        onRefreshTablesClicked();
    } else {
        appendLog(QString("❌ Şema kontrol hatası: %1").arg(DbManager::instance().lastError()), "#EF4444");
    }
}

void DbDebugWindow::onLoadProfilesClicked() {
    if (!DbManager::instance().isConnected()) return;
    appendLog("Varsayılan test profilleri yükleniyor...", "#89DCEB");
    bool ok = DbManager::instance().ensureDefaultDataIfMissing();
    if (ok) {
        appendLog("✔ Varsayılan uydu profilleri ve transponder listesi yüklendi.", "#34D399");
        onRefreshTablesClicked();
    }
}

void DbDebugWindow::onInsertMockRunClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("Test kaydı eklenemedi: Veritabanı bağlı değil.", "#EF4444");
        return;
    }

    TestRunData mock;
    mock.antennaSn = QString("MOCK-TEST-%1").arg(QDateTime::currentSecsSinceEpoch() % 10000);
    mock.stationId = m_spinStationId->value();
    mock.testerName = "Debug Runner";
    mock.operatorUsername = "admin";
    mock.satelliteProfileId = 1;
    mock.profileName = "Türksat Standart";
    mock.timestamp = QDateTime::currentDateTime();
    mock.durationSec = 15;
    mock.overallResult = "PASS";
    mock.avgDeltaDb = 0.45;
    mock.maxDeltaDb = 0.90;
    mock.notes = "Manuel debug testi kaydı";

    qint64 runId = DbManager::instance().insertTestRun(mock);
    if (runId > 0) {
        appendLog(QString("✔ Mock test kaydı eklendi (#ID: %1 | Seri: %2 | Sonuç: PASS)")
                      .arg(runId).arg(mock.antennaSn), "#34D399");
        onRefreshTablesClicked();
    } else {
        appendLog(QString("❌ Mock test ekleme hatası: %1").arg(DbManager::instance().lastError()), "#EF4444");
    }
}

void DbDebugWindow::onAcquireLockClicked() {
    int st = m_spinStationId->value();
    int ch = m_spinChannelNo->value();

    int lockedBy = 0, curCh = 0;
    bool ok = DbManager::instance().tryAcquireDbLock(st, lockedBy, curCh);
    if (ok) {
        appendLog(QString("✔ Kilit ALINDI: Masa %1 referans cihazını kilitledi (Kanal: %2)").arg(st).arg(ch), "#34D399");
        updateLockBadge(st, ch);
    } else {
        appendLog(QString("⚠️ Kilit ALINAMADI: Referans şu anda Masa %1 tarafından kilitli!").arg(lockedBy), "#F59E0B");
        updateLockBadge(lockedBy, curCh);
    }
}

void DbDebugWindow::onReleaseLockClicked() {
    int st = m_spinStationId->value();
    bool ok = DbManager::instance().releaseDbLock(st);
    if (ok) {
        appendLog(QString("✔ Kilit BIRAKILDI: Masa %1 kilidi serbest bıraktı.").arg(st), "#34D399");
    } else {
        appendLog(QString("⚠️ Kilit bırakma sorgusu tamamlandı."), "#FAB387");
    }
    onQueryLockClicked();
}

void DbDebugWindow::onHeartbeatClicked() {
    int st = m_spinStationId->value();
    int ch = m_spinChannelNo->value();
    bool ok = DbManager::instance().updateDbLockHeartbeat(st, ch);
    if (ok) {
        appendLog(QString("💓 Heartbeat güncellendi: Masa %1, Kanal %2").arg(st).arg(ch), "#89DCEB");
    } else {
        appendLog("⚠️ Heartbeat güncellenemedi (Kilit sizde olmayabilir).", "#F59E0B");
    }
    onQueryLockClicked();
}

void DbDebugWindow::onQueryLockClicked() {
    if (!DbManager::instance().isConnected()) return;

    int lockedBy = 0, curCh = 0;
    QSqlDatabase db = QSqlDatabase::database(DbManager::instance().connectionName());
    QSqlQuery q("SELECT locked_by_station, current_channel_no FROM reference_lock WHERE id = 1 LIMIT 1", db);
    if (q.next()) {
        lockedBy = q.value(0).toInt();
        curCh = q.value(1).toInt();
    }
    updateLockBadge(lockedBy, curCh);
}

void DbDebugWindow::onStartEngineClicked() {
    appendLog("Taşınabilir MariaDB motoru başlatılıyor...", "#89DCEB");
    QString cmd = "tools/mariadb_portable/start_db.bat";
    QProcess::startDetached("cmd.exe", QStringList() << "/c" << cmd);
    QTimer::singleShot(1500, this, &DbDebugWindow::onConnectClicked);
}

void DbDebugWindow::onStopEngineClicked() {
    appendLog("Taşınabilir MariaDB motoru durduruluyor...", "#F59E0B");
    QString cmd = "tools/mariadb_portable/stop_db.bat";
    QProcess::startDetached("cmd.exe", QStringList() << "/c" << cmd);
    QTimer::singleShot(1000, this, [this]() {
        updateConnectionBadge(false);
    });
}

void DbDebugWindow::onClearLogsClicked() {
    if (m_txtLogs) m_txtLogs->clear();
}
