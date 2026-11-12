/**
 * @file db_viewer_widget.cpp
 * @brief Implementation of the Administrator SQL database table browser widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "db_viewer_widget.h"
#include "core/app_config.h"
#include "database/db_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QAbstractItemView>

DbViewerWidget::DbViewerWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();
}

void DbViewerWidget::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *grp = new QGroupBox("🗄️ Veritabanı İçerik Görüntüleyici (MariaDB / SQL)", this);
    auto *vLay = new QVBoxLayout(grp);
    vLay->setSpacing(8);

    auto *bar = new QHBoxLayout();
    bar->setSpacing(8);

    bar->addWidget(new QLabel("Tablo:", grp));
    m_comboTables = new QComboBox(grp);
    m_comboTables->setMinimumWidth(210);
    connect(m_comboTables, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DbViewerWidget::loadSelectedTable);
    bar->addWidget(m_comboTables);

    bar->addWidget(new QLabel("Limit:", grp));
    m_comboLimit = new QComboBox(grp);
    m_comboLimit->addItem("200 Satır", 200);
    m_comboLimit->addItem("500 Satır", 500);
    m_comboLimit->addItem("1000 Satır", 1000);
    connect(m_comboLimit, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DbViewerWidget::loadSelectedTable);
    bar->addWidget(m_comboLimit);

    m_txtFilter = new QLineEdit(grp);
    m_txtFilter->setPlaceholderText("Tablo içinde ara / filtrele...");
    m_txtFilter->setClearButtonEnabled(true);
    connect(m_txtFilter, &QLineEdit::textChanged, this, &DbViewerWidget::applyFilter);
    bar->addWidget(m_txtFilter, 1);

    m_btnRefresh = new QPushButton("🔄 Yenile", grp);
    m_btnRefresh->setCursor(Qt::PointingHandCursor);
    m_btnRefresh->setStyleSheet("background-color: #059669; color: white; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(m_btnRefresh, &QPushButton::clicked, this, &DbViewerWidget::refreshAll);
    bar->addWidget(m_btnRefresh);

    vLay->addLayout(bar);

    m_lblDbInfo = new QLabel("Veritabanı durumu bekleniyor...", grp);
    m_lblDbInfo->setStyleSheet("color: #34D399; font-weight: bold; padding: 2px 4px;");
    vLay->addWidget(m_lblDbInfo);

    m_table = new QTableWidget(grp);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    vLay->addWidget(m_table, 1);

    root->addWidget(grp);
}

void DbViewerWidget::refreshAll() {
    DbManager &mgr = DbManager::instance();
    if (!mgr.isConnected()) {
        m_knownTables.clear();
        m_comboTables->blockSignals(true);
        m_comboTables->clear();
        m_comboTables->blockSignals(false);
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
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
            while (q.next()) {
                tables << q.value(0).toString();
            }
        }
    } else {
        if (q.exec("SHOW TABLES;")) {
            while (q.next()) {
                tables << q.value(0).toString();
            }
        }
    }

    m_knownTables = tables;
    m_comboTables->blockSignals(true);
    m_comboTables->clear();
    m_comboTables->addItems(tables);
    int idx = tables.indexOf(prevSelection);
    if (idx >= 0) {
        m_comboTables->setCurrentIndex(idx);
    }
    m_comboTables->blockSignals(false);

    loadSelectedTable();
}

void DbViewerWidget::loadSelectedTable() {
    DbManager &mgr = DbManager::instance();
    if (!mgr.isConnected()) return;

    const QString tableName = m_comboTables->currentText().trimmed();
    if (tableName.isEmpty() || !m_knownTables.contains(tableName)) {
        m_table->setRowCount(0);
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

    m_table->setUpdatesEnabled(false);
    m_table->clear();
    m_table->setColumnCount(colCount);

    QStringList headers;
    for (int c = 0; c < colCount; ++c) {
        headers << rec.fieldName(c);
    }
    m_table->setHorizontalHeaderLabels(headers);

    int rowCount = 0;
    m_table->setRowCount(0);
    while (q.next()) {
        m_table->insertRow(rowCount);
        for (int c = 0; c < colCount; ++c) {
            const QVariant val = q.value(c);
            const QString text = val.isNull() ? QStringLiteral("NULL") : val.toString();
            auto *item = new QTableWidgetItem(text);
            if (val.isNull()) {
                item->setForeground(QColor("#64748B"));
            }
            m_table->setItem(rowCount, c, item);
        }
        ++rowCount;
    }
    m_table->resizeColumnsToContents();
    m_table->setUpdatesEnabled(true);

    const QString dbHost = db.hostName().isEmpty() ? AppConfig::instance().dbHost() : db.hostName();
    const int dbPort = db.port() > 0 ? db.port() : AppConfig::instance().dbPort();
    const QString dbName = db.databaseName().isEmpty() ? AppConfig::instance().dbName() : db.databaseName();

    m_lblDbInfo->setText(QString("🟢 Sunucu: %1:%2 (%3)  |  Tablo: %4  |  Kayıt: %5  |  Sütun: %6  |  Sürücü: %7")
                             .arg(dbHost)
                             .arg(dbPort)
                             .arg(dbName)
                             .arg(tableName)
                             .arg(rowCount)
                             .arg(colCount)
                             .arg(mgr.activeDriver()));
    m_lblDbInfo->setStyleSheet("color: #34D399; font-weight: bold;");

    if (!m_txtFilter->text().trimmed().isEmpty()) {
        applyFilter(m_txtFilter->text());
    }
}

void DbViewerWidget::applyFilter(const QString &text) {
    const QString needle = text.trimmed();
    const int rows = m_table->rowCount();
    const int cols = m_table->columnCount();

    for (int r = 0; r < rows; ++r) {
        if (needle.isEmpty()) {
            m_table->setRowHidden(r, false);
            continue;
        }
        bool match = false;
        for (int c = 0; c < cols; ++c) {
            QTableWidgetItem *it = m_table->item(r, c);
            if (it && it->text().contains(needle, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        m_table->setRowHidden(r, !match);
    }
}
