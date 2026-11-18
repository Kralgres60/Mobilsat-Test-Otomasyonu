/**
 * @file test_history_widget.cpp
 * @brief Implementation of Test History, Database Table Viewer, and Audit Logs.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "test_history_widget.h"
#include "core/app_config.h"
#include "core/xlsxutil.h"
#include "core/pdf_report_generator.h"
#include "ui/common/spc_analysis_widget.h"
#include "database/db_manager.h"
#include <QDesktopServices>
#include <QUrl>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTabBar>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
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
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->tabBar()->setExpanding(false);

    auto *tabHistory = new QWidget(m_tabs);
    setupHistoryTab(tabHistory);
    m_tabs->addTab(tabHistory, "📋 Test Koşumları & Geçmiş");

    auto *tabDb = new QWidget(m_tabs);
    setupDbViewerTab(tabDb);
    m_tabs->addTab(tabDb, "🗄️ Veritabanı Tabloları (SQL)");

    auto *tabAudit = new QWidget(m_tabs);
    setupAuditLogTab(tabAudit);
    m_tabs->addTab(tabAudit, "📜 Sistem Olay & Denetim Logları (Audit)");

    m_spcWidget = new SpcAnalysisWidget(m_tabs);
    connect(m_spcWidget, &SpcAnalysisWidget::logMessage, this, &TestHistoryWidget::logMessage);
    m_tabs->addTab(m_spcWidget, "📊 İstatistiksel Süreç Kontrolü (SPC & Gauss)");

    root->addWidget(m_tabs);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == 0) refreshHistory();
        else if (idx == 1) refreshDbTables();
        else if (idx == 2) refreshAuditLogs();
        else if (idx == 3 && m_spcWidget) m_spcWidget->refreshAnalysis();
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

    m_btnExportPdf = new QPushButton("📄 PDF Sertifika Üret", tab);
    m_btnExportPdf->setStyleSheet("background-color: #DC2626; color: white; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnExportPdf, &QPushButton::clicked, this, &TestHistoryWidget::onExportPdfClicked);
    filterRow->addWidget(m_btnExportPdf);

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
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &TestHistoryWidget::onTableContextMenuRequested);
    vbox->addWidget(m_table, 1);

    // Pagination toolbar
    QHBoxLayout *pageRow = new QHBoxLayout();
    pageRow->setSpacing(6);

    m_btnFirstPage = new QPushButton("⏮ İlk", tab);
    m_btnFirstPage->setCursor(Qt::PointingHandCursor);
    m_btnFirstPage->setFixedWidth(55);
    connect(m_btnFirstPage, &QPushButton::clicked, this, &TestHistoryWidget::onFirstPageClicked);

    m_btnPrevPage = new QPushButton("◀ Önceki", tab);
    m_btnPrevPage->setCursor(Qt::PointingHandCursor);
    m_btnPrevPage->setFixedWidth(75);
    connect(m_btnPrevPage, &QPushButton::clicked, this, &TestHistoryWidget::onPrevPageClicked);

    m_lblPageInfo = new QLabel("Sayfa 1 / 1", tab);
    m_lblPageInfo->setStyleSheet("font-weight: bold; color: #34D399; padding: 0 8px; font-size: 10pt;");

    m_btnNextPage = new QPushButton("Sonraki ▶", tab);
    m_btnNextPage->setCursor(Qt::PointingHandCursor);
    m_btnNextPage->setFixedWidth(75);
    connect(m_btnNextPage, &QPushButton::clicked, this, &TestHistoryWidget::onNextPageClicked);

    m_btnLastPage = new QPushButton("Son ⏭", tab);
    m_btnLastPage->setCursor(Qt::PointingHandCursor);
    m_btnLastPage->setFixedWidth(55);
    connect(m_btnLastPage, &QPushButton::clicked, this, &TestHistoryWidget::onLastPageClicked);

    pageRow->addWidget(m_btnFirstPage);
    pageRow->addWidget(m_btnPrevPage);
    pageRow->addWidget(m_lblPageInfo);
    pageRow->addWidget(m_btnNextPage);
    pageRow->addWidget(m_btnLastPage);
    pageRow->addStretch();

    pageRow->addWidget(new QLabel("Sayfa Başına:", tab));
    m_comboPageSize = new QComboBox(tab);
    m_comboPageSize->addItem("50 Kayıt", 50);
    m_comboPageSize->addItem("100 Kayıt", 100);
    m_comboPageSize->addItem("250 Kayıt", 250);
    m_comboPageSize->addItem("500 Kayıt", 500);
    connect(m_comboPageSize, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestHistoryWidget::onPageSizeChanged);
    pageRow->addWidget(m_comboPageSize);

    m_lblTotalCount = new QLabel("Toplam: 0 Kayıt", tab);
    m_lblTotalCount->setStyleSheet("color: #94A3B8; font-size: 9pt; padding-left: 10px;");
    pageRow->addWidget(m_lblTotalCount);

    vbox->addLayout(pageRow);
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
    m_currentPage = 1;
    applyFilters();
}

void TestHistoryWidget::applyFilters() {
    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        m_cachedRuns.clear();
        m_table->setRowCount(0);
        m_lblSummary->setText("⚠️ Veritabanı bağlantısı yok (Çevrimdışı)");
        m_totalRecords = 0;
        m_totalPages = 1;
        updatePaginationControls();
        return;
    }

    const QString query = m_txtSearch ? m_txtSearch->text().trimmed() : QString();
    const int stationFilter = m_comboStation ? m_comboStation->currentData().toInt() : -1;
    const QString resultFilter = m_comboResult ? m_comboResult->currentData().toString() : QString();

    // 1. Get total count using indexed DB query
    m_totalRecords = db.getTestRunsCount(query, stationFilter, resultFilter);
    m_totalPages = qMax(1, (m_totalRecords + m_pageSize - 1) / m_pageSize);
    m_currentPage = qBound(1, m_currentPage, m_totalPages);

    // 2. Fetch only the requested page slice using indexed LIMIT & OFFSET
    m_cachedRuns = db.getTestRunsPaged(m_currentPage, m_pageSize, query, stationFilter, resultFilter);

    // 3. Rebuild in-memory hash index for instantaneous O(1) barcode lookup
    rebuildBarcodeIndex();

    // 4. Render rows and update pagination UI
    populateTable(m_cachedRuns);
    updatePaginationControls();
}

void TestHistoryWidget::updatePaginationControls() {
    if (m_lblPageInfo) {
        m_lblPageInfo->setText(QString("Sayfa %1 / %2").arg(m_currentPage).arg(m_totalPages));
    }
    if (m_lblTotalCount) {
        m_lblTotalCount->setText(QString("Toplam: %1 Kayıt").arg(m_totalRecords));
    }
    if (m_btnFirstPage) m_btnFirstPage->setEnabled(m_currentPage > 1);
    if (m_btnPrevPage)  m_btnPrevPage->setEnabled(m_currentPage > 1);
    if (m_btnNextPage)  m_btnNextPage->setEnabled(m_currentPage < m_totalPages);
    if (m_btnLastPage)  m_btnLastPage->setEnabled(m_currentPage < m_totalPages);
}

void TestHistoryWidget::rebuildBarcodeIndex() {
    m_barcodeIndex.clear();
    for (int i = 0; i < m_cachedRuns.size(); ++i) {
        const QString sn = m_cachedRuns[i].antennaSn.trimmed().toUpper();
        if (!sn.isEmpty()) {
            m_barcodeIndex[sn].append(i);
        }
    }
}

int TestHistoryWidget::binarySearchRunById(const QVector<TestRunData> &runs, qint64 targetId) {
    int left = 0;
    int right = runs.size() - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (runs[mid].id == targetId) {
            return mid;
        }
        if (runs[mid].id < targetId) {
            right = mid - 1; // Since runs are sorted descending by ID
        } else {
            left = mid + 1;
        }
    }
    return -1;
}

void TestHistoryWidget::onFirstPageClicked() {
    if (m_currentPage != 1) {
        m_currentPage = 1;
        applyFilters();
    }
}

void TestHistoryWidget::onPrevPageClicked() {
    if (m_currentPage > 1) {
        m_currentPage--;
        applyFilters();
    }
}

void TestHistoryWidget::onNextPageClicked() {
    if (m_currentPage < m_totalPages) {
        m_currentPage++;
        applyFilters();
    }
}

void TestHistoryWidget::onLastPageClicked() {
    if (m_currentPage != m_totalPages) {
        m_currentPage = m_totalPages;
        applyFilters();
    }
}

void TestHistoryWidget::onPageSizeChanged(int /*index*/) {
    if (m_comboPageSize) {
        m_pageSize = m_comboPageSize->currentData().toInt();
        m_currentPage = 1;
        applyFilters();
    }
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
    m_table->resizeColumnsToContents();
    for (int c = 0; c < m_table->columnCount() - 1; ++c) {
        m_table->setColumnWidth(c, m_table->columnWidth(c) + 16);
    }
    m_lblSummary->setText(QString("📊 Sayfa %1/%2: %3 kayıt gösteriliyor | Toplam Filtrelenen: %4 (Bu sayfada: ✅ %5  ❌ %6  ⚠️ %7)")
                              .arg(m_currentPage).arg(m_totalPages).arg(runs.size())
                              .arg(m_totalRecords).arg(passCount).arg(failCount).arg(abortCount));
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

void TestHistoryWidget::onExportPdfClicked() {
    int row = m_table->currentRow();
    if (row < 0 || row >= m_cachedRuns.size()) {
        QMessageBox::information(this, "Seçim Yapılmadı",
            "Lütfen Kalite Kontrol Sertifikası üretmek istediğiniz test kaydını tablodan seçiniz.");
        return;
    }

    const TestRunData &run = m_cachedRuns.at(row);
    QVector<ChannelSampleResult> channels = DbManager::instance().getTestRunChannels(run.id);

    QString defaultPath = PdfReportGenerator::defaultCertificatePath(run.antennaSn, run.id);
    QString savePath = QFileDialog::getSaveFileName(
        this,
        "Kalite Kontrol Sertifikasını Kaydet (PDF)",
        defaultPath,
        "PDF Dosyaları (*.pdf);;Tüm Dosyalar (*.*)");

    if (savePath.isEmpty()) return;

    QString err;
    if (PdfReportGenerator::generateCertificate(run, channels, savePath, &err)) {
        emit logMessage(QString("📄 PDF Kalite Sertifikası üretildi: %1").arg(QFileInfo(savePath).fileName()));
        auto reply = QMessageBox::question(
            this,
            "Sertifika Hazır",
            QString("Kalite Kontrol Sertifikası başarıyla oluşturuldu:\n\n%1\n\nDosyayı şimdi açmak ister misiniz?")
                .arg(QFileInfo(savePath).fileName()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

        if (reply == QMessageBox::Yes) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
        }
    } else {
        QMessageBox::warning(this, "Sertifika Üretilemedi",
            QString("PDF belgesi oluşturulurken hata meydana geldi:\n\n%1").arg(err));
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
// DATABASE TABLE BROWSER (TAB 2)
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
    m_tableDb->resizeColumnsToContents();
    for (int c = 0; c < colCount; ++c) {
        m_tableDb->setColumnWidth(c, qMin(420, m_tableDb->columnWidth(c) + 14));
    }
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

void TestHistoryWidget::setupAuditLogTab(QWidget *tab) {
    auto *vbox = new QVBoxLayout(tab);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(6);

    QHBoxLayout *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);

    filterRow->addWidget(new QLabel("Ara / Filtre:", tab));
    m_txtAuditSearch = new QLineEdit(tab);
    m_txtAuditSearch->setPlaceholderText("Olay türü, operatör veya detay filtrele...");
    connect(m_txtAuditSearch, &QLineEdit::textChanged, this, &TestHistoryWidget::refreshAuditLogs);
    filterRow->addWidget(m_txtAuditSearch, 2);

    m_btnAuditRefresh = new QPushButton("🔄 Yenile", tab);
    connect(m_btnAuditRefresh, &QPushButton::clicked, this, &TestHistoryWidget::refreshAuditLogs);
    filterRow->addWidget(m_btnAuditRefresh);

    m_btnAuditClear = new QPushButton("🗑️ Denetim Loglarını Temizle", tab);
    m_btnAuditClear->setStyleSheet("background-color: #B91C1C; color: white; border-radius: 4px; padding: 4px 10px; font-weight: bold;");
    connect(m_btnAuditClear, &QPushButton::clicked, this, &TestHistoryWidget::onClearAuditLogsClicked);
    filterRow->addWidget(m_btnAuditClear);

    filterRow->addStretch();
    m_lblAuditSummary = new QLabel("0 denetim kaydı listelendi", tab);
    m_lblAuditSummary->setStyleSheet("color: #34D399; font-weight: bold;");
    filterRow->addWidget(m_lblAuditSummary);

    vbox->addLayout(filterRow);

    m_tableAudit = new QTableWidget(tab);
    m_tableAudit->setColumnCount(6);
    m_tableAudit->setHorizontalHeaderLabels({
        "ID", "Zaman Damgası", "Olay / Eylem Türü", "İstasyon", "Operatör", "Detaylar & Açıklama"
    });
    m_tableAudit->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableAudit->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_tableAudit->verticalHeader()->setVisible(false);
    m_tableAudit->setAlternatingRowColors(true);
    m_tableAudit->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableAudit->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vbox->addWidget(m_tableAudit, 1);
}

void TestHistoryWidget::refreshAuditLogs() {
    if (!m_tableAudit) return;
    QString search = m_txtAuditSearch ? m_txtAuditSearch->text().trimmed() : QString();
    QVector<AuditLogRecord> logs = DbManager::instance().getAuditLogs(300, search);

    m_tableAudit->setRowCount(0);
    for (int i = 0; i < logs.size(); ++i) {
        const auto &log = logs[i];
        m_tableAudit->insertRow(i);
        m_tableAudit->setItem(i, 0, new QTableWidgetItem(QString::number(log.id)));
        m_tableAudit->setItem(i, 1, new QTableWidgetItem(log.timestamp.toString("yyyy-MM-dd HH:mm:ss")));

        QTableWidgetItem *actItem = new QTableWidgetItem(log.actionType);
        if (log.actionType.contains("FINISH") || log.actionType.contains("PASS")) {
            actItem->setForeground(QBrush(QColor("#34D399")));
        } else if (log.actionType.contains("FAIL") || log.actionType.contains("DELETE")) {
            actItem->setForeground(QBrush(QColor("#F87171")));
        } else {
            actItem->setForeground(QBrush(QColor("#38BDF8")));
        }
        actItem->setFont(QFont("Segoe UI", 9, QFont::Bold));
        m_tableAudit->setItem(i, 2, actItem);

        QString stStr = (log.stationId == 0) ? "Host (0)" : QString("İstasyon %1").arg(log.stationId);
        m_tableAudit->setItem(i, 3, new QTableWidgetItem(stStr));
        m_tableAudit->setItem(i, 4, new QTableWidgetItem(log.operatorUsername));
        m_tableAudit->setItem(i, 5, new QTableWidgetItem(log.details));
    }

    m_tableAudit->resizeColumnsToContents();
    for (int c = 0; c < m_tableAudit->columnCount() - 1; ++c) {
        m_tableAudit->setColumnWidth(c, m_tableAudit->columnWidth(c) + 16);
    }

    if (m_lblAuditSummary) {
        m_lblAuditSummary->setText(QString("%1 denetim kaydı listelendi").arg(logs.size()));
    }
}

void TestHistoryWidget::onClearAuditLogsClicked() {
    auto ans = QMessageBox::question(this, "Denetim Kayıtlarını Temizle",
                                     "Tüm sistem ve olay denetim kayıtlarını silmek istediğinize emin misiniz?",
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans == QMessageBox::Yes) {
        DbManager::instance().clearAuditLogs();
        refreshAuditLogs();
        emit logMessage("[Denetim Log] Sistem olay kayıtları temizlendi.");
    }
}

void TestHistoryWidget::onTableContextMenuRequested(const QPoint &pos) {
    if (!m_table) return;
    QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid()) return;

    int row = idx.row();
    m_table->selectRow(row);

    qint64 runId = m_table->item(row, 0) ? m_table->item(row, 0)->text().toLongLong() : 0;
    if (runId <= 0) return;

    QString sn = m_table->item(row, 6) ? m_table->item(row, 6)->text() : QString();
    QString dateStr = m_table->item(row, 1) ? m_table->item(row, 1)->text() : QString();

    QMenu menu(this);
    QAction *actDelete = menu.addAction(QString("🗑️ Test Kaydını Sil (ID #%1)").arg(runId));
    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));

    if (chosen == actDelete) {
        auto reply = QMessageBox::question(
            this,
            "Test Kaydı Silme Onayı",
            QString("Seçili test kaydını kalıcı olarak silmek istediğinize emin misiniz?\n\n"
                    "• Kayıt ID: #%1\n"
                    "• Anten Seri No: %2\n"
                    "• Tarih: %3\n\n"
                    "Bu işlem geri alınamaz ve kanala ait ölçümler de silinir.")
                .arg(runId)
                .arg(sn.isEmpty() ? "--" : sn)
                .arg(dateStr),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            if (DbManager::instance().deleteTestRun(runId)) {
                DbManager::instance().logAudit(
                    "TEST_RUN_DELETED",
                    QString("Deleted test run #%1 (SN: %2)").arg(runId).arg(sn),
                    AppConfig::instance().currentUsername(),
                    AppConfig::instance().stationId());
                emit logMessage(QString("[Test Geçmişi] Test kaydı #%1 (SN: %2) silindi.").arg(runId).arg(sn));
                refreshHistory();
            } else {
                QMessageBox::warning(this, "Silme Hatası",
                                     "Kayıt veritabanından silinemedi:\n" + DbManager::instance().lastError());
            }
        }
    }
}
