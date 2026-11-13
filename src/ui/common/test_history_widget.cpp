/**
 * @file test_history_widget.cpp
 * @brief Ortak Test Geçmişi & Veritabanı Tablo Tarayıcısı uygulaması.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "test_history_widget.h"
#include "core/app_config.h"
#include "core/xlsxutil.h"
#include "database/db_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QColor>
#include <QAbstractItemView>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>

TestHistoryWidget::TestHistoryWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();
}

void TestHistoryWidget::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_tabs = new QTabWidget(this);

    auto *tabHistory = new QWidget(m_tabs);
    setupHistoryTab(tabHistory);
    m_tabs->addTab(tabHistory, "📋 Test Koşumları & Geçmiş");

    auto *tabDb = new QWidget(m_tabs);
    setupDbViewerTab(tabDb);
    m_tabs->addTab(tabDb, "🗄️ Veritabanı Tabloları (SQL)");

    root->addWidget(m_tabs);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == 0) refreshHistory();
        else if (idx == 1) refreshDbTables();
    });
}

void TestHistoryWidget::setupHistoryTab(QWidget *tab) {
    auto *vbox = new QVBoxLayout(tab);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(6);

    QHBoxLayout *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);

    filterRow->addWidget(new QLabel("Ara:", tab));
    m_txtSearch = new QLineEdit(tab);
    m_txtSearch->setPlaceholderText("Seri No, İsim, Kullanıcı, Profil veya Detay...");
    connect(m_txtSearch, &QLineEdit::returnPressed, this, &TestHistoryWidget::refreshHistory);
    connect(m_txtSearch, &QLineEdit::textChanged, this, &TestHistoryWidget::applyFilters);
    filterRow->addWidget(m_txtSearch, 2);

    filterRow->addWidget(new QLabel("Kaynak:", tab));
    m_comboStation = new QComboBox(tab);
    m_comboStation->addItem("Tümü", -1);
    m_comboStation->addItem("Yönetici (Host)", 0);
    m_comboStation->addItem("İstasyon 1", 1);
    m_comboStation->addItem("İstasyon 2", 2);
    m_comboStation->addItem("İstasyon 3", 3);
    connect(m_comboStation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestHistoryWidget::applyFilters);
    filterRow->addWidget(m_comboStation);

    filterRow->addWidget(new QLabel("Sonuç:", tab));
    m_comboResult = new QComboBox(tab);
    m_comboResult->addItem("Tümü", "");
    m_comboResult->addItem("PASS", "PASS");
    m_comboResult->addItem("FAIL", "FAIL");
    m_comboResult->addItem("ABORTED", "ABORTED");
    connect(m_comboResult, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestHistoryWidget::applyFilters);
    filterRow->addWidget(m_comboResult);

    m_btnRefresh = new QPushButton("🔄 Yenile", tab);
    connect(m_btnRefresh, &QPushButton::clicked, this, &TestHistoryWidget::refreshHistory);
    filterRow->addWidget(m_btnRefresh);

    m_btnExportXlsx = new QPushButton("📤 Excel'e Aktar (.xlsx)", tab);
    m_btnExportXlsx->setStyleSheet("background-color: #059669; color: white; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnExportXlsx, &QPushButton::clicked, this, &TestHistoryWidget::onExportExcelClicked);
    filterRow->addWidget(m_btnExportXlsx);

    m_btnImportXlsx = new QPushButton("📥 Excel'den Al (.xlsx)", tab);
    m_btnImportXlsx->setStyleSheet("background-color: #0284C7; color: white; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnImportXlsx, &QPushButton::clicked, this, &TestHistoryWidget::onImportExcelClicked);
    filterRow->addWidget(m_btnImportXlsx);

    vbox->addLayout(filterRow);

    m_lblSummary = new QLabel("Kayıtlar yükleniyor...", tab);
    m_lblSummary->setStyleSheet("color: #A7C4B5; font-size: 9.5pt; padding: 2px 4px;");
    vbox->addWidget(m_lblSummary);

    m_table = new QTableWidget(tab);
    m_table->setColumnCount(13);
    m_table->setHorizontalHeaderLabels({
        "ID", "Tarih / Saat", "İstasyon / Kaynak", "Testi Yapan", "Kullanıcı Adı",
        "Test / Profil", "Anten Seri No", "İş Emri", "Süre (sn)",
        "Sonuç", "Ort. Δ (dB)", "Max Δ (dB)", "Detay / Notlar"
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setDefaultSectionSize(26);
    vbox->addWidget(m_table, 1);
}

void TestHistoryWidget::setupDbViewerTab(QWidget *tab) {
    auto *vbox = new QVBoxLayout(tab);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(6);

    QHBoxLayout *bar = new QHBoxLayout();
    bar->setSpacing(8);

    bar->addWidget(new QLabel("Tablo:", tab));
    m_comboTables = new QComboBox(tab);
    m_comboTables->setMinimumWidth(200);
    connect(m_comboTables, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestHistoryWidget::loadSelectedDbTable);
    bar->addWidget(m_comboTables);

    bar->addWidget(new QLabel("Limit:", tab));
    m_comboLimit = new QComboBox(tab);
    m_comboLimit->addItem("200 Satır", 200);
    m_comboLimit->addItem("500 Satır", 500);
    m_comboLimit->addItem("1000 Satır", 1000);
    connect(m_comboLimit, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestHistoryWidget::loadSelectedDbTable);
    bar->addWidget(m_comboLimit);

    m_txtDbFilter = new QLineEdit(tab);
    m_txtDbFilter->setPlaceholderText("Tablo içinde ara / filtrele...");
    m_txtDbFilter->setClearButtonEnabled(true);
    connect(m_txtDbFilter, &QLineEdit::textChanged, this, &TestHistoryWidget::applyDbFilter);
    bar->addWidget(m_txtDbFilter, 1);

    m_btnDbRefresh = new QPushButton("🔄 Tabloları Yenile", tab);
    m_btnDbRefresh->setCursor(Qt::PointingHandCursor);
    m_btnDbRefresh->setStyleSheet("background-color: #059669; color: white; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(m_btnDbRefresh, &QPushButton::clicked, this, &TestHistoryWidget::refreshDbTables);
    bar->addWidget(m_btnDbRefresh);

    vbox->addLayout(bar);

    m_lblDbInfo = new QLabel("Veritabanı durumu bekleniyor...", tab);
    m_lblDbInfo->setStyleSheet("color: #34D399; font-weight: bold; padding: 2px 4px;");
    vbox->addWidget(m_lblDbInfo);

    m_tableDb = new QTableWidget(tab);
    m_tableDb->setAlternatingRowColors(true);
    m_tableDb->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableDb->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableDb->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableDb->verticalHeader()->setVisible(false);
    m_tableDb->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableDb->horizontalHeader()->setStretchLastSection(true);
    vbox->addWidget(m_tableDb, 1);
}

void TestHistoryWidget::refreshHistory() {
    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        m_cachedRuns.clear();
        m_table->setRowCount(0);
        m_lblSummary->setText("⚠️ Veritabanı bağlantısı yok (Çevrimdışı)");
        return;
    }

    m_cachedRuns = db.getTestRuns(500);
    applyFilters();
}

void TestHistoryWidget::applyFilters() {
    const QString query = m_txtSearch->text().trimmed();
    const int stationFilter = m_comboStation->currentData().toInt();
    const QString resultFilter = m_comboResult->currentData().toString();

    QVector<TestRunData> filtered;
    filtered.reserve(m_cachedRuns.size());

    for (const auto &r : m_cachedRuns) {
        if (stationFilter != -1 && r.stationId != stationFilter) continue;
        if (!resultFilter.isEmpty() && !r.overallResult.trimmed().toUpper().contains(resultFilter)) continue;

        if (!query.isEmpty()) {
            bool match = r.antennaSn.contains(query, Qt::CaseInsensitive)
                      || r.testerName.contains(query, Qt::CaseInsensitive)
                      || r.operatorUsername.contains(query, Qt::CaseInsensitive)
                      || r.profileName.contains(query, Qt::CaseInsensitive)
                      || r.failReason.contains(query, Qt::CaseInsensitive)
                      || QString::number(r.workOrderId).contains(query);
            if (!match) continue;
        }

        filtered.append(r);
    }

    populateTable(filtered);
}

void TestHistoryWidget::populateTable(const QVector<TestRunData> &runs) {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(runs.size());

    int passCount = 0;
    int failCount = 0;
    int abortCount = 0;

    for (int i = 0; i < runs.size(); ++i) {
        const auto &r = runs.at(i);

        QString stationLabel;
        if (r.stationId == 0) stationLabel = "Yönetici (Host)";
        else stationLabel = QString("İstasyon %1").arg(r.stationId);

        QString resUpper = r.overallResult.trimmed().toUpper();
        if (resUpper == "PASS") passCount++;
        else if (resUpper == "FAIL") failCount++;
        else abortCount++;

        QColor resColor;
        if (resUpper == "PASS") resColor = QColor("#059669");
        else if (resUpper == "FAIL") resColor = QColor("#DC2626");
        else resColor = QColor("#D97706");

        auto setCell = [this, i](int col, const QString &text, const QColor *fg = nullptr) {
            auto *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            if (fg) item->setForeground(*fg);
            m_table->setItem(i, col, item);
        };

        setCell(0, QString::number(r.id));
        setCell(1, r.timestamp.isValid() ? r.timestamp.toString("yyyy-MM-dd HH:mm:ss") : "-");
        setCell(2, stationLabel);
        setCell(3, r.testerName.isEmpty() ? "-" : r.testerName);
        setCell(4, r.operatorUsername.isEmpty() ? "-" : r.operatorUsername);
        setCell(5, r.profileName.isEmpty() ? "-" : r.profileName);
        setCell(6, r.antennaSn);
        setCell(7, r.workOrderId > 0 ? QString::number(r.workOrderId) : "-");
        setCell(8, QString::number(r.durationSec));
        setCell(9, r.overallResult, &resColor);
        setCell(10, QString::number(r.avgDeltaDb, 'f', 2));
        setCell(11, QString::number(r.maxDeltaDb, 'f', 2));

        auto *itemNote = new QTableWidgetItem(r.failReason.isEmpty() ? "-" : r.failReason);
        m_table->setItem(i, 12, itemNote);
    }

    m_table->setSortingEnabled(true);
    m_lblSummary->setText(QString("Toplam: %1 kayıt listelendi  |  ✅ PASS: %2   ❌ FAIL: %3   ⚠️ Diğer: %4")
                              .arg(runs.size()).arg(passCount).arg(failCount).arg(abortCount));
}

void TestHistoryWidget::onExportExcelClicked() {
    if (m_table->rowCount() == 0) {
        QMessageBox::information(this, "Bilgi", "Dışa aktarılacak kayıt bulunmuyor.");
        return;
    }

    QString defaultName = QString("NETA_Test_Raporu_%1.xlsx")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString fileName = QFileDialog::getSaveFileName(
        this, "Excel Olarak Kaydet", defaultName, "Excel Dosyası (*.xlsx)");

    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".xlsx", Qt::CaseInsensitive)) fileName += ".xlsx";

    QStringList headers;
    for (int col = 0; col < m_table->columnCount(); ++col) {
        headers << m_table->horizontalHeaderItem(col)->text();
    }

    QVector<QStringList> rows;
    rows.reserve(m_table->rowCount());
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList rowData;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            auto *it = m_table->item(r, c);
            rowData << (it ? it->text() : "");
        }
        rows.append(rowData);
    }

    QString errStr;
    if (writeXlsxSheet(fileName, "Test Gecmisi", headers, rows, &errStr)) {
        emit logMessage(QString("[Excel] %1 kayıt dışa aktarıldı: %2").arg(rows.size()).arg(fileName));
        QMessageBox::information(this, "Başarılı",
                                 QString("%1 kayıt Excel dosyasına aktarıldı:\n%2")
                                     .arg(rows.size()).arg(fileName));
    } else {
        QMessageBox::critical(this, "Hata", "Excel dosyası oluşturulamadı:\n" + errStr);
    }
}

void TestHistoryWidget::onImportExcelClicked() {
    QString fileName = QFileDialog::getOpenFileName(
        this, "Excel Dosyası Seç", "", "Excel Dosyası (*.xlsx)");
    if (fileName.isEmpty()) return;

    QStringList headers;
    QVector<QStringList> rows;
    QString err;

    if (!readXlsxSheet(fileName, headers, rows, &err)) {
        QMessageBox::critical(this, "Okuma Hatası", "Excel dosyası okunamadı:\n" + err);
        return;
    }

    if (rows.isEmpty()) {
        QMessageBox::warning(this, "Uyarı", "Excel dosyası geçerli veri satırı içermiyor.");
        return;
    }

    int snCol = -1, resCol = -1, testerCol = -1, profCol = -1;
    for (int c = 0; c < headers.size(); ++c) {
        const QString h = headers[c].toLower();
        if (h.contains("seri") || h.contains("sn")) snCol = c;
        else if (h.contains("sonuç") || h.contains("result")) resCol = c;
        else if (h.contains("yapan") || h.contains("tester")) testerCol = c;
        else if (h.contains("profil") || h.contains("test")) profCol = c;
    }

    if (snCol == -1) snCol = qMin(6, headers.size() - 1);
    if (resCol == -1) resCol = qMin(9, headers.size() - 1);

    DbManager &db = DbManager::instance();
    int importedCount = 0;

    for (const auto &row : rows) {
        if (row.size() <= snCol) continue;
        const QString sn = row[snCol].trimmed();
        if (sn.isEmpty()) continue;

        TestRunData tr;
        tr.antennaSn = sn;
        tr.timestamp = QDateTime::currentDateTime();
        tr.overallResult = (resCol >= 0 && row.size() > resCol) ? row[resCol].trimmed() : "PASS";
        tr.testerName = (testerCol >= 0 && row.size() > testerCol) ? row[testerCol].trimmed() : "Excel Import";
        tr.profileName = (profCol >= 0 && row.size() > profCol) ? row[profCol].trimmed() : "Imported";
        tr.failReason = "Excel dosyasından içe aktarıldı.";

        if (db.insertTestRun(tr) > 0) {
            importedCount++;
        }
    }

    refreshHistory();
    QMessageBox::information(this, "İçe Aktarma Tamamlandı",
                             QString("Excel dosyasından %1 kayıt başarıyla veritabanına aktarıldı.").arg(importedCount));
}

// =============================================================================
// VERİTABANI TABLO TARAYICISI (TAB 2)
// =============================================================================
void TestHistoryWidget::refreshDbTables() {
    DbManager &mgr = DbManager::instance();
    if (!mgr.isConnected()) {
        m_knownTables.clear();
        m_comboTables->blockSignals(true);
        m_comboTables->clear();
        m_comboTables->blockSignals(false);
        m_tableDb->setRowCount(0);
        m_tableDb->setColumnCount(0);
        m_lblDbInfo->setText("🔴 Veritabanı bağlı değil!");
        m_lblDbInfo->setStyleSheet("color: #F87171; font-weight: bold;");
        return;
    }

    const QString prevSelection = m_comboTables->currentText();
    QSqlDatabase db = QSqlDatabase::database(mgr.connectionName());
    QSqlQuery q(db);

    QStringList tables;
    if (mgr.activeDriver() == "QSQLITE") {
        if (q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name;")) {
            while (q.next()) tables << q.value(0).toString();
        }
    } else {
        if (q.exec("SHOW TABLES;")) {
            while (q.next()) tables << q.value(0).toString();
        }
    }

    m_knownTables = tables;
    m_comboTables->blockSignals(true);
    m_comboTables->clear();
    m_comboTables->addItems(tables);
    int idx = tables.indexOf(prevSelection);
    if (idx >= 0) m_comboTables->setCurrentIndex(idx);
    m_comboTables->blockSignals(false);

    loadSelectedDbTable();
}

void TestHistoryWidget::loadSelectedDbTable() {
    DbManager &mgr = DbManager::instance();
    if (!mgr.isConnected()) return;

    const QString tableName = m_comboTables->currentText().trimmed();
    if (tableName.isEmpty() || !m_knownTables.contains(tableName)) {
        m_tableDb->setRowCount(0);
        return;
    }

    const int limit = m_comboLimit->currentData().toInt();
    QSqlDatabase db = QSqlDatabase::database(mgr.connectionName());
    QSqlQuery q(db);

    const QString sql = (mgr.activeDriver() == "QSQLITE")
                            ? QString("SELECT * FROM \"%1\" LIMIT %2;").arg(tableName).arg(limit)
                            : QString("SELECT * FROM `%1` LIMIT %2;").arg(tableName).arg(limit);

    if (!q.exec(sql)) {
        m_lblDbInfo->setText(QString("🔴 Sorgu hatası (%1): %2").arg(tableName, q.lastError().text()));
        m_lblDbInfo->setStyleSheet("color: #F87171; font-weight: bold;");
        return;
    }

    const QSqlRecord rec = q.record();
    const int colCount = rec.count();

    m_tableDb->setUpdatesEnabled(false);
    m_tableDb->clear();
    m_tableDb->setColumnCount(colCount);

    QStringList headerLabels;
    headerLabels.reserve(colCount);
    for (int c = 0; c < colCount; ++c) {
        headerLabels << rec.fieldName(c);
    }
    m_tableDb->setHorizontalHeaderLabels(headerLabels);

    int rowIdx = 0;
    while (q.next()) {
        m_tableDb->insertRow(rowIdx);
        for (int c = 0; c < colCount; ++c) {
            const QVariant v = q.value(c);
            QString displayStr = v.isNull() ? "NULL" : v.toString();
            auto *item = new QTableWidgetItem(displayStr);
            if (v.isNull()) item->setForeground(QColor("#9CA3AF"));
            m_tableDb->setItem(rowIdx, c, item);
        }
        rowIdx++;
    }

    m_tableDb->setUpdatesEnabled(true);
    m_lblDbInfo->setText(QString("🟢 Tablo: %1  |  Listelenen: %2 satır (Limit: %3)  |  Sürücü: %4")
                             .arg(tableName).arg(rowIdx).arg(limit).arg(mgr.activeDriver()));
    m_lblDbInfo->setStyleSheet("color: #34D399; font-weight: bold;");

    applyDbFilter(m_txtDbFilter->text());
}

void TestHistoryWidget::applyDbFilter(const QString &text) {
    const QString filter = text.trimmed();
    const int rowCount = m_tableDb->rowCount();
    const int colCount = m_tableDb->columnCount();

    if (filter.isEmpty()) {
        for (int r = 0; r < rowCount; ++r) m_tableDb->setRowHidden(r, false);
        return;
    }

    for (int r = 0; r < rowCount; ++r) {
        bool match = false;
        for (int c = 0; c < colCount; ++c) {
            auto *it = m_tableDb->item(r, c);
            if (it && it->text().contains(filter, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        m_tableDb->setRowHidden(r, !match);
    }
}
