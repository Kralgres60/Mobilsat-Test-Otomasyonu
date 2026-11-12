/**
 * @file test_history_widget.cpp
 * @brief Implementation of TestHistoryWidget with filtering and Excel (.xlsx) export/import.
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
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QBrush>
#include <QColor>
#include <QAbstractItemView>
#include <QRegularExpression>

TestHistoryWidget::TestHistoryWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();
}

void TestHistoryWidget::setupUi() {
    QVBoxLayout *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(8);

    // ── Header & Filter Bar ──────────────────────────────────────────────────
    QHBoxLayout *filterRow = new QHBoxLayout();
    filterRow->setSpacing(6);

    QLabel *lblSearch = new QLabel("Ara:", this);
    m_txtSearch = new QLineEdit(this);
    m_txtSearch->setPlaceholderText("Seri No, İsim, Kullanıcı, Profil veya Detay...");
    connect(m_txtSearch, &QLineEdit::returnPressed, this, &TestHistoryWidget::refreshHistory);
    connect(m_txtSearch, &QLineEdit::textChanged, this, &TestHistoryWidget::applyFilters);

    QLabel *lblStation = new QLabel("Kaynak:", this);
    m_comboStation = new QComboBox(this);
    m_comboStation->addItem("Tümü", -1);
    m_comboStation->addItem("Yönetici (Host)", 0);
    m_comboStation->addItem("İstasyon 1", 1);
    m_comboStation->addItem("İstasyon 2", 2);
    m_comboStation->addItem("İstasyon 3", 3);
    connect(m_comboStation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestHistoryWidget::applyFilters);

    QLabel *lblResult = new QLabel("Sonuç:", this);
    m_comboResult = new QComboBox(this);
    m_comboResult->addItem("Tümü", "");
    m_comboResult->addItem("PASS", "PASS");
    m_comboResult->addItem("FAIL", "FAIL");
    m_comboResult->addItem("ABORTED", "ABORTED");
    connect(m_comboResult, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TestHistoryWidget::applyFilters);

    m_btnRefresh = new QPushButton("🔄 Yenile", this);
    connect(m_btnRefresh, &QPushButton::clicked, this, &TestHistoryWidget::refreshHistory);

    m_btnExportXlsx = new QPushButton("📤 Excel'e Aktar (.xlsx)", this);
    m_btnExportXlsx->setStyleSheet(
        "background-color: #059669; color: #FFFFFF; border: none; "
        "border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnExportXlsx, &QPushButton::clicked, this, &TestHistoryWidget::onExportExcelClicked);

    m_btnImportXlsx = new QPushButton("📥 Excel'den Al (.xlsx)", this);
    m_btnImportXlsx->setStyleSheet(
        "background-color: #0284C7; color: #FFFFFF; border: none; "
        "border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnImportXlsx, &QPushButton::clicked, this, &TestHistoryWidget::onImportExcelClicked);

    filterRow->addWidget(lblSearch);
    filterRow->addWidget(m_txtSearch, 2);
    filterRow->addWidget(lblStation);
    filterRow->addWidget(m_comboStation);
    filterRow->addWidget(lblResult);
    filterRow->addWidget(m_comboResult);
    filterRow->addWidget(m_btnRefresh);
    filterRow->addWidget(m_btnExportXlsx);
    filterRow->addWidget(m_btnImportXlsx);
    vbox->addLayout(filterRow);

    // ── Summary Strip ────────────────────────────────────────────────────────
    m_lblSummary = new QLabel("Kayıtlar yükleniyor...", this);
    m_lblSummary->setStyleSheet("color: #A7C4B5; font-size: 9.5pt; padding: 2px 4px;");
    vbox->addWidget(m_lblSummary);

    // ── Operations & Test History Table ──────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(13);
    m_table->setHorizontalHeaderLabels({
        "ID", "Tarih / Saat", "İstasyon / Kaynak", "Testi Yapan", "Kullanıcı Adı",
        "Test / Profil", "Anten Seri No", "İş Emri", "Süre (sn)",
        "Sonuç", "Ort. Δ (dB)", "Max Δ (dB)", "Detay / Notlar"
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(false);

    // Sensible default widths
    m_table->setColumnWidth(0, 52);
    m_table->setColumnWidth(1, 138);
    m_table->setColumnWidth(2, 125);
    m_table->setColumnWidth(3, 120);
    m_table->setColumnWidth(4, 105);
    m_table->setColumnWidth(5, 175);
    m_table->setColumnWidth(6, 130);
    m_table->setColumnWidth(7, 65);
    m_table->setColumnWidth(8, 70);
    m_table->setColumnWidth(9, 85);
    m_table->setColumnWidth(10, 82);
    m_table->setColumnWidth(11, 82);

    vbox->addWidget(m_table, 1);
}

void TestHistoryWidget::refreshHistory() {
    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        db.connectToDatabase(AppConfig::instance().dbHost(), AppConfig::instance().dbPort(),
                             AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                             AppConfig::instance().dbPassword());
    }

    if (!db.isConnected()) {
        m_cachedRuns.clear();
        m_table->setRowCount(0);
        m_lblSummary->setText("⚠️ Veritabanı bağlantısı kurulamadı: " + db.lastError());
        return;
    }

    m_cachedRuns = db.getTestRuns(500, QString());
    applyFilters();
}

void TestHistoryWidget::applyFilters() {
    const QString needle = m_txtSearch ? m_txtSearch->text().trimmed().toLower() : QString();
    const int stationFilter = m_comboStation ? m_comboStation->currentData().toInt() : -1;
    const QString resultFilter = m_comboResult ? m_comboResult->currentData().toString() : QString();

    QVector<TestRunData> filtered;
    filtered.reserve(m_cachedRuns.size());

    for (const auto &r : m_cachedRuns) {
        if (stationFilter >= 0 && r.stationId != stationFilter) {
            continue;
        }
        if (!resultFilter.isEmpty() && r.overallResult.compare(resultFilter, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (!needle.isEmpty()) {
            const QString stationText = (r.stationId == 0)
                                            ? QStringLiteral("yönetici host")
                                            : QString("istasyon %1").arg(r.stationId);
            const bool matches =
                r.antennaSn.toLower().contains(needle) ||
                r.testerName.toLower().contains(needle) ||
                r.operatorUsername.toLower().contains(needle) ||
                r.profileName.toLower().contains(needle) ||
                r.failReason.toLower().contains(needle) ||
                r.notes.toLower().contains(needle) ||
                stationText.contains(needle);
            if (!matches) continue;
        }
        filtered.append(r);
    }

    populateTable(filtered);
}

void TestHistoryWidget::populateTable(const QVector<TestRunData> &runs) {
    m_table->setRowCount(runs.size());

    int passCount = 0;
    int failCount = 0;
    int abortedCount = 0;

    for (int i = 0; i < runs.size(); ++i) {
        const auto &r = runs[i];
        if (r.overallResult == "PASS") passCount++;
        else if (r.overallResult == "ABORTED") abortedCount++;
        else failCount++;

        const QString sourceLabel = (r.stationId == 0)
                                        ? QStringLiteral("Yönetici (Host)")
                                        : QString("İstasyon %1").arg(r.stationId);
        const QString detailText = !r.failReason.trimmed().isEmpty()
                                       ? r.failReason
                                       : r.notes;

        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(r.id)));
        m_table->setItem(i, 1, new QTableWidgetItem(r.timestamp.isValid()
                                                        ? r.timestamp.toString("dd.MM.yyyy HH:mm:ss")
                                                        : "--"));
        m_table->setItem(i, 2, new QTableWidgetItem(sourceLabel));
        m_table->setItem(i, 3, new QTableWidgetItem(r.testerName.isEmpty() ? "--" : r.testerName));
        m_table->setItem(i, 4, new QTableWidgetItem(r.operatorUsername.isEmpty() ? "--" : r.operatorUsername));
        m_table->setItem(i, 5, new QTableWidgetItem(r.profileName.isEmpty() ? "--" : r.profileName));
        m_table->setItem(i, 6, new QTableWidgetItem(r.antennaSn));
        m_table->setItem(i, 7, new QTableWidgetItem(r.workOrderId > 0 ? QString::number(r.workOrderId) : "-"));
        m_table->setItem(i, 8, new QTableWidgetItem(QString::number(r.durationSec)));

        QTableWidgetItem *resItem = new QTableWidgetItem(r.overallResult);
        if (r.overallResult == "PASS") {
            resItem->setForeground(QBrush(QColor("#34D399")));
        } else if (r.overallResult == "ABORTED") {
            resItem->setForeground(QBrush(QColor("#FBBF24")));
        } else {
            resItem->setForeground(QBrush(QColor("#F87171")));
        }
        m_table->setItem(i, 9, resItem);
        m_table->setItem(i, 10, new QTableWidgetItem(QString::number(r.avgDeltaDb, 'f', 2)));
        m_table->setItem(i, 11, new QTableWidgetItem(QString::number(r.maxDeltaDb, 'f', 2)));
        m_table->setItem(i, 12, new QTableWidgetItem(detailText));
    }

    DbManager &db = DbManager::instance();
    m_lblSummary->setText(
        QString("Toplam Kayıt: %1  |  ✔ PASS: %2  |  ✖ FAIL: %3  |  ⏹ ABORTED: %4  |  Veritabanı: %5:%6")
            .arg(runs.size())
            .arg(passCount)
            .arg(failCount)
            .arg(abortedCount)
            .arg(db.connectedHost())
            .arg(db.connectedPort()));
}

void TestHistoryWidget::onExportExcelClicked() {
    if (m_table->rowCount() == 0) {
        QMessageBox::information(this, "Bilgi", "Dışa aktarılacak kayıt bulunmuyor.");
        return;
    }

    const QString defaultName = QString("NETA_Test_Gecmisi_%1.xlsx")
                                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString fileName = QFileDialog::getSaveFileName(
        this, "Excel Raporunu Kaydet", defaultName, "Excel Dosyaları (*.xlsx)");
    if (fileName.isEmpty()) return;
    if (!fileName.endsWith(".xlsx", Qt::CaseInsensitive)) {
        fileName += ".xlsx";
    }

    QStringList headers;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        auto *hItem = m_table->horizontalHeaderItem(c);
        headers << (hItem ? hItem->text() : QString("Sütun %1").arg(c + 1));
    }

    QVector<QStringList> rows;
    rows.reserve(m_table->rowCount());
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList rowData;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            auto *item = m_table->item(r, c);
            rowData << (item ? item->text() : QString());
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
        this, "Excel'den Test Geçmişi Al", QString(), "Excel Dosyaları (*.xlsx)");
    if (fileName.isEmpty()) return;

    QStringList headers;
    QVector<QStringList> rows;
    QString errStr;
    if (!readXlsxSheet(fileName, headers, rows, &errStr)) {
        QMessageBox::critical(this, "Okuma Hatası", "Excel dosyası okunamadı:\n" + errStr);
        return;
    }

    if (rows.isEmpty()) {
        QMessageBox::warning(this, "Boş Dosya", "Excel dosyasında içe aktarılacak veri satırı bulunamadı.");
        return;
    }

    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        QMessageBox::critical(this, "Veritabanı Hatası", "İçe aktarma için aktif veritabanı bağlantısı gerekli.");
        return;
    }

    // Header index lookup (case-insensitive, supports exported format + common aliases)
    auto findCol = [&](const QStringList &aliases, int fallbackIdx) -> int {
        for (int i = 0; i < headers.size(); ++i) {
            const QString h = headers[i].trimmed().toLower();
            for (const QString &alias : aliases) {
                if (h.contains(alias)) return i;
            }
        }
        return (fallbackIdx < headers.size()) ? fallbackIdx : -1;
    };

    const int colTimestamp = findCol({"tarih", "date", "time"}, 1);
    const int colStation   = findCol({"istasyon", "kaynak", "masa", "station"}, 2);
    const int colTester    = findCol({"testi yapan", "tester", "kişi"}, 3);
    const int colUsername  = findCol({"kullanıcı", "username", "user"}, 4);
    const int colProfile   = findCol({"profil", "test / profil", "profile"}, 5);
    const int colSn        = findCol({"seri no", "serial", "anten"}, 6);
    const int colWorkOrder = findCol({"iş emri", "order", "wo"}, 7);
    const int colDuration  = findCol({"süre", "duration"}, 8);
    const int colResult    = findCol({"sonuç", "result"}, 9);
    const int colAvgDelta  = findCol({"ort.", "avg"}, 10);
    const int colMaxDelta  = findCol({"max"}, 11);
    const int colNotes     = findCol({"detay", "not", "neden", "fail", "note"}, 12);

    auto cellAt = [](const QStringList &row, int idx) -> QString {
        return (idx >= 0 && idx < row.size()) ? row[idx].trimmed() : QString();
    };

    auto parseDouble = [](QString s) -> double {
        s.remove("dB", Qt::CaseInsensitive).replace(',', '.').trimmed();
        return s.toDouble();
    };

    int importedCount = 0;
    int skippedCount = 0;

    for (const QStringList &row : rows) {
        const QString sn = cellAt(row, colSn);
        if (sn.isEmpty()) {
            skippedCount++;
            continue;
        }

        TestRunData run;
        run.antennaSn = sn;

        // Parse timestamp
        const QString tsStr = cellAt(row, colTimestamp);
        QDateTime dt = QDateTime::fromString(tsStr, "dd.MM.yyyy HH:mm:ss");
        if (!dt.isValid()) dt = QDateTime::fromString(tsStr, Qt::ISODate);
        if (!dt.isValid()) dt = QDateTime::fromString(tsStr, "yyyy-MM-dd HH:mm:ss");
        if (!dt.isValid()) dt = QDateTime::currentDateTime();
        run.timestamp = dt;

        // Parse station ID
        const QString stStr = cellAt(row, colStation);
        if (stStr.contains("yönetici", Qt::CaseInsensitive) || stStr.contains("host", Qt::CaseInsensitive)) {
            run.stationId = 0;
        } else {
            QRegularExpression re("(\\d+)");
            auto m = re.match(stStr);
            run.stationId = m.hasMatch() ? m.captured(1).toInt() : 1;
        }

        run.testerName = cellAt(row, colTester);
        if (run.testerName == "--") run.testerName.clear();

        run.operatorUsername = cellAt(row, colUsername);
        if (run.operatorUsername == "--") run.operatorUsername.clear();

        run.profileName = cellAt(row, colProfile);
        if (run.profileName == "--") run.profileName.clear();

        const QString woStr = cellAt(row, colWorkOrder);
        run.workOrderId = (woStr == "-" || woStr.isEmpty()) ? 0 : woStr.toInt();

        run.durationSec = cellAt(row, colDuration).toInt();

        QString res = cellAt(row, colResult).toUpper();
        if (res != "PASS" && res != "FAIL" && res != "ABORTED") {
            res = "PASS";
        }
        run.overallResult = res;

        run.avgDeltaDb = parseDouble(cellAt(row, colAvgDelta));
        run.maxDeltaDb = parseDouble(cellAt(row, colMaxDelta));

        const QString detail = cellAt(row, colNotes);
        if (res == "FAIL" || res == "ABORTED") {
            run.failReason = detail;
        } else {
            run.notes = detail;
        }

        if (db.testRunExists(run.antennaSn, run.stationId, run.timestamp)) {
            skippedCount++;
            continue;
        }

        if (db.insertTestRun(run) > 0) {
            importedCount++;
        } else {
            skippedCount++;
        }
    }

    refreshHistory();
    emit logMessage(QString("[Excel] İçe aktarma tamamlandı: %1 eklendi, %2 atlandı.").arg(importedCount).arg(skippedCount));
    QMessageBox::information(
        this, "İçe Aktarma Tamamlandı",
        QString("Excel dosyasından %1 kayıt başarıyla veritabanına aktarıldı.\nAtlanan (mükerrer/boş) satır: %2")
            .arg(importedCount).arg(skippedCount));
}
