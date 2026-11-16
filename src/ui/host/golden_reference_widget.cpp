/**
 * @file golden_reference_widget.cpp
 * @brief Implementation of Host direct measurement-based Golden Reference manager.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "golden_reference_widget.h"
#include "database/db_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QDateTime>
#include <numeric>

GoldenReferenceWidget::GoldenReferenceWidget(DrxWebClient *refWeb, SerialWorker *refSerial, QWidget *parent)
    : QWidget(parent),
      m_refWebClient(refWeb),
      m_refSerial(refSerial)
{
    setupUi();

    m_recordTimer.setInterval(1000);
    connect(&m_recordTimer, &QTimer::timeout, this, &GoldenReferenceWidget::onRecordTick);

    if (m_refWebClient) {
        connect(m_refWebClient, &DrxWebClient::telemetryReceived, this,
                [this](double powDbm, double cnDb, double, double) {
            onReferenceTelemetryReceived(powDbm, cnDb);
        });
    }
    if (m_refSerial) {
        connect(m_refSerial, &SerialWorker::telemetryReceived, this,
                [this](double powDbm, double cnDb, double) {
            onReferenceTelemetryReceived(powDbm, cnDb);
        });
    }

    reloadFrequencies();
}

void GoldenReferenceWidget::setupUi() {
    QVBoxLayout *rootVBox = new QVBoxLayout(this);
    rootVBox->setContentsMargins(10, 10, 10, 10);
    rootVBox->setSpacing(10);

    // =========================================================================
    // 1. Host Reference Mode Selector
    // =========================================================================
    QGroupBox *grpRefMode = new QGroupBox("🎯 Yönetici Referans Karşılaştırma Modu (Tüm İstasyonları Bağlar)", this);
    QHBoxLayout *modeHBox = new QHBoxLayout(grpRefMode);
    modeHBox->setSpacing(10);

    QLabel *lblModeInfo = new QLabel(
        "Test istasyonları yönetici onaylı referans kaynağını kullanır. "
        "Fiziksel referans anteni yokken kayıtlı Golden Sample moduna geçebilirsiniz:",
        grpRefMode);
    lblModeInfo->setWordWrap(true);
    lblModeInfo->setStyleSheet("color: #A7C4B5;");

    m_comboRefMode = new QComboBox(grpRefMode);
    m_comboRefMode->addItem("🟢 Canlı Referans Anten (Live Reference)", static_cast<int>(ReferenceMode::LiveReference));
    m_comboRefMode->addItem("🌟 Kayıtlı Golden Sample (Referans Anten Yokken)", static_cast<int>(ReferenceMode::GoldenSample));
    m_comboRefMode->setMinimumWidth(320);

    QPushButton *btnApplyMode = new QPushButton("✅ Modu Uygula & İstasyonlara Bildir", grpRefMode);
    btnApplyMode->setCursor(Qt::PointingHandCursor);
    btnApplyMode->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnApplyMode, &QPushButton::clicked, this, &GoldenReferenceWidget::onApplyRefModeClicked);

    modeHBox->addWidget(lblModeInfo, 1);
    modeHBox->addWidget(m_comboRefMode);
    modeHBox->addWidget(btnApplyMode);
    rootVBox->addWidget(grpRefMode);

    // =========================================================================
    // 2. Direct Measurement Controls (Live Audio-Style Recording & Fixed Values)
    // =========================================================================
    QGroupBox *grpControls = new QGroupBox("🎙️ Doğrudan Golden Sample Ölçüm ve Kayıt Paneli", this);
    QVBoxLayout *ctrlVBox = new QVBoxLayout(grpControls);
    ctrlVBox->setSpacing(8);

    // Row A: Live Record Button ("Ses kaydeder gibi başlat & kaydet")
    QHBoxLayout *rowRecord = new QHBoxLayout();
    m_btnLiveRecord = new QPushButton("🔴 Canlı Ölçüm Kaydını Başlat", grpControls);
    m_btnLiveRecord->setCursor(Qt::PointingHandCursor);
    m_btnLiveRecord->setStyleSheet("background-color: #B91C1C; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; font-size: 10pt;");
    connect(m_btnLiveRecord, &QPushButton::clicked, this, &GoldenReferenceWidget::onToggleLiveRecordClicked);

    m_lblLiveRecordStatus = new QLabel("⚪ Kayıt beklemede. Referans alıcıdan canlı telemetri kaydetmek için butona basınız.", grpControls);
    m_lblLiveRecordStatus->setStyleSheet("color: #94A3B8; font-weight: bold; padding-left: 8px;");

    rowRecord->addWidget(m_btnLiveRecord);
    rowRecord->addWidget(m_lblLiveRecordStatus, 1);
    ctrlVBox->addLayout(rowRecord);

    // Row B: Direct Fixed Value Entry ("Direk sabit bir değer girip onuda kaydedebilcez")
    QHBoxLayout *rowFixed = new QHBoxLayout();
    rowFixed->setSpacing(8);
    rowFixed->addWidget(new QLabel("Sabit Ölçüm Adı:"));

    m_txtFixedLabel = new QLineEdit("Sabit Referans - 14.5 dB", grpControls);
    m_txtFixedLabel->setFixedWidth(190);
    rowFixed->addWidget(m_txtFixedLabel);

    rowFixed->addWidget(new QLabel("Sabit C/N:"));
    m_spinFixedCn = new QDoubleSpinBox(grpControls);
    m_spinFixedCn->setRange(0.0, 30.0);
    m_spinFixedCn->setSingleStep(0.1);
    m_spinFixedCn->setValue(14.50);
    m_spinFixedCn->setSuffix(" dB");
    m_spinFixedCn->setFixedWidth(90);
    rowFixed->addWidget(m_spinFixedCn);

    rowFixed->addWidget(new QLabel("Sabit Güç:"));
    m_spinFixedPow = new QDoubleSpinBox(grpControls);
    m_spinFixedPow->setRange(-100.0, 0.0);
    m_spinFixedPow->setSingleStep(0.5);
    m_spinFixedPow->setValue(-42.00);
    m_spinFixedPow->setSuffix(" dBm");
    m_spinFixedPow->setFixedWidth(100);
    rowFixed->addWidget(m_spinFixedPow);

    rowFixed->addWidget(new QLabel("Tolerans:"));
    m_spinFixedTol = new QDoubleSpinBox(grpControls);
    m_spinFixedTol->setRange(0.10, 10.0);
    m_spinFixedTol->setSingleStep(0.1);
    m_spinFixedTol->setValue(1.50);
    m_spinFixedTol->setSuffix(" dB");
    m_spinFixedTol->setFixedWidth(85);
    rowFixed->addWidget(m_spinFixedTol);

    m_btnAddFixed = new QPushButton("➕ Sabit Değer Kaydet", grpControls);
    m_btnAddFixed->setCursor(Qt::PointingHandCursor);
    m_btnAddFixed->setStyleSheet("background-color: #0284C7; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(m_btnAddFixed, &QPushButton::clicked, this, &GoldenReferenceWidget::onAddFixedMeasurementClicked);
    rowFixed->addWidget(m_btnAddFixed);

    rowFixed->addStretch();
    ctrlVBox->addLayout(rowFixed);

    rootVBox->addWidget(grpControls);

    // =========================================================================
    // 3. Measurements Table & Visual Mini Graph
    // =========================================================================
    QGroupBox *grpTable = new QGroupBox("📋 Kayıtlı Golden Sample Referans Ölçümleri Listesi", this);
    QVBoxLayout *tableVBox = new QVBoxLayout(grpTable);
    tableVBox->setSpacing(8);

    m_tableMeasurements = new QTableWidget(grpTable);
    m_tableMeasurements->setColumnCount(8);
    m_tableMeasurements->setHorizontalHeaderLabels({
        "#", "Ölçüm Adı / Etiket", "Ortalama C/N", "Ortalama Güç", "Tolerans", "Kayıt Türü & Süre", "Tarih / Saat", "Durum"
    });
    m_tableMeasurements->verticalHeader()->setVisible(false);
    m_tableMeasurements->setAlternatingRowColors(true);
    m_tableMeasurements->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableMeasurements->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableMeasurements->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableMeasurements->setColumnWidth(0, 36);
    m_tableMeasurements->setColumnWidth(2, 110);
    m_tableMeasurements->setColumnWidth(3, 115);
    m_tableMeasurements->setColumnWidth(4, 95);
    m_tableMeasurements->setColumnWidth(5, 130);
    m_tableMeasurements->setColumnWidth(6, 150);
    m_tableMeasurements->setColumnWidth(7, 100);
    connect(m_tableMeasurements, &QTableWidget::itemSelectionChanged, this, &GoldenReferenceWidget::onTableSelectionChanged);
    tableVBox->addWidget(m_tableMeasurements, 1);

    // Visual Mini Graph
    m_lblPlotTitle = new QLabel("📊 Seçili Ölçüm Analiz Grafiği: (Grafiği görmek için tablodan bir ölçüme tıklayınız)", grpTable);
    m_lblPlotTitle->setStyleSheet("color: #89DCEB; font-weight: bold; margin-top: 4px;");
    tableVBox->addWidget(m_lblPlotTitle);

    m_miniPlot = new QCustomPlot(grpTable);
    setupMiniPlot();
    tableVBox->addWidget(m_miniPlot);

    // Action Buttons below Table
    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnDel = new QPushButton("🗑️ Seçili Ölçümü Sil", grpTable);
    btnDel->setCursor(Qt::PointingHandCursor);
    btnDel->setStyleSheet("background-color: #991B1B; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(btnDel, &QPushButton::clicked, this, &GoldenReferenceWidget::onRemoveSelectedRowClicked);

    QPushButton *btnSaveAll = new QPushButton("💾 Tüm Değişiklikleri Veritabanına Kaydet", grpTable);
    btnSaveAll->setCursor(Qt::PointingHandCursor);
    btnSaveAll->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 7px 16px; font-weight: bold;");
    connect(btnSaveAll, &QPushButton::clicked, this, &GoldenReferenceWidget::onSaveAllClicked);

    btnRow->addWidget(btnDel);
    btnRow->addStretch();
    btnRow->addWidget(btnSaveAll);
    tableVBox->addLayout(btnRow);

    m_lblStatus = new QLabel(
        "İpucu: Canlı ölçüm 'Ses Kaydı' gibi başlatılıp durdurularak kesintisiz kaydedilebilir veya doğrudan sabit değer girilebilir. "
        "Tablodaki herhangi bir satıra tıklandığında ölçülen C/N ve tolerans eğrisi yukarıdaki mini grafikte anlık görüntülenir.",
        grpTable);
    m_lblStatus->setWordWrap(true);
    m_lblStatus->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");
    tableVBox->addWidget(m_lblStatus);

    rootVBox->addWidget(grpTable, 1);
}

void GoldenReferenceWidget::setupMiniPlot() {
    if (!m_miniPlot) return;
    m_miniPlot->setBackground(QBrush(QColor("#181825")));
    m_miniPlot->setFixedHeight(175);

    m_miniPlot->xAxis->setBasePen(QPen(QColor("#45475A")));
    m_miniPlot->yAxis->setBasePen(QPen(QColor("#45475A")));
    m_miniPlot->xAxis->setTickPen(QPen(QColor("#45475A")));
    m_miniPlot->yAxis->setTickPen(QPen(QColor("#45475A")));
    m_miniPlot->xAxis->setTickLabelColor(QColor("#CDD6F4"));
    m_miniPlot->yAxis->setTickLabelColor(QColor("#CDD6F4"));
    m_miniPlot->xAxis->setLabelColor(QColor("#A6E3A1"));
    m_miniPlot->yAxis->setLabelColor(QColor("#A6E3A1"));
    m_miniPlot->xAxis->setLabel("Zaman / Örnek No (sn)");
    m_miniPlot->yAxis->setLabel("Referans C/N (dB)");
    m_miniPlot->xAxis->grid()->setPen(QPen(QColor("#313244"), 1, Qt::DotLine));
    m_miniPlot->yAxis->grid()->setPen(QPen(QColor("#313244"), 1, Qt::DotLine));

    // Graph 0: C/N Telemetry Curve
    m_miniPlot->addGraph();
    m_miniPlot->graph(0)->setPen(QPen(QColor("#34D399"), 2));
    m_miniPlot->graph(0)->setBrush(QBrush(QColor(52, 211, 153, 30)));
    m_miniPlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, QPen(QColor("#FFFFFF"), 2), QBrush(QColor("#10B981")), 5));
    m_miniPlot->graph(0)->setName("C/N Değeri");

    // Graph 1: Baseline Average Line
    m_miniPlot->addGraph();
    m_miniPlot->graph(1)->setPen(QPen(QColor("#FFFFFF"), 1.5, Qt::SolidLine));
    m_miniPlot->graph(1)->setName("Ortalama Baseline");

    // Graph 2: Upper Tolerance Band
    m_miniPlot->addGraph();
    m_miniPlot->graph(2)->setPen(QPen(QColor("#38BDF8"), 1, Qt::DashLine));
    m_miniPlot->graph(2)->setName("+Tolerans");

    // Graph 3: Lower Tolerance Band
    m_miniPlot->addGraph();
    m_miniPlot->graph(3)->setPen(QPen(QColor("#F87171"), 1, Qt::DashLine));
    m_miniPlot->graph(3)->setName("-Tolerans");

    m_miniPlot->xAxis->setRange(0.0, 10.0);
    m_miniPlot->yAxis->setRange(0.0, 20.0);
}

void GoldenReferenceWidget::setActiveReferenceMode(ReferenceMode mode) {
    if (m_comboRefMode) {
        m_comboRefMode->setCurrentIndex(mode == ReferenceMode::GoldenSample ? 1 : 0);
    }
}

void GoldenReferenceWidget::onApplyRefModeClicked() {
    if (!m_comboRefMode) return;
    emit referenceModeApplyRequested(static_cast<ReferenceMode>(m_comboRefMode->currentData().toInt()));
}

void GoldenReferenceWidget::onReferenceTelemetryReceived(double powDbm, double cnDb) {
    if (!m_isRecording) return;

    m_currentRecCn.append(cnDb);
    m_currentRecPow.append(powDbm);
    m_currentRecTime.append(m_recordingElapsedSec + (m_currentRecCn.size() % 10) * 0.1);

    // Live update mini plot during recording
    if (m_miniPlot && m_currentRecCn.size() > 1) {
        m_miniPlot->xAxis->setRange(0.0, qMax(10.0, m_currentRecTime.last() + 2.0));
        m_miniPlot->graph(0)->setData(m_currentRecTime, m_currentRecCn);
        m_miniPlot->replot();
    }
}

void GoldenReferenceWidget::onToggleLiveRecordClicked() {
    if (!m_isRecording) {
        // Start Audio-Style Live Recording
        bool refReady = (m_refWebClient && m_refWebClient->isReachable()) ||
                        (m_refSerial && m_refSerial->isConnected());
        if (!refReady) {
            QMessageBox::warning(this, "Referans Alıcı Bağlı Değil",
                                 "Canlı referans kaydı yapabilmek için Referans DRX9000 alıcısının bağlı olması gerekir.\n\n"
                                 "İpucu: Alıcı bağlı değilse aşağıdaki 'Sabit Değer Kaydet' panelinden sabit referans değeri girebilirsiniz.");
            return;
        }

        m_isRecording = true;
        m_recordingElapsedSec = 0;
        m_currentRecCn.clear();
        m_currentRecPow.clear();
        m_currentRecTime.clear();

        m_btnLiveRecord->setText("⏹️ Kaydı Bitir & Listeye Ekle");
        m_btnLiveRecord->setStyleSheet("background-color: #DC2626; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; font-size: 10pt;");
        m_lblLiveRecordStatus->setText("🔴 KAYDEDİLİYOR... 00:00 sn | Referans alıcıdan canlı telemetri alınıyor...");
        m_lblLiveRecordStatus->setStyleSheet("color: #F87171; font-weight: bold; padding-left: 8px;");

        m_lblPlotTitle->setText("📊 Canlı Kaydedilen Referans Ölçüm Eğrisi");
        m_miniPlot->graph(0)->data()->clear();
        m_miniPlot->graph(1)->data()->clear();
        m_miniPlot->graph(2)->data()->clear();
        m_miniPlot->graph(3)->data()->clear();
        m_miniPlot->replot();

        m_recordTimer.start();
        emit logMessage("[Golden Reference] Canlı telemetri kaydı başlatıldı.");
    } else {
        // Stop Recording & Save to List
        m_isRecording = false;
        m_recordTimer.stop();

        m_btnLiveRecord->setText("🔴 Canlı Ölçüm Kaydını Başlat");
        m_btnLiveRecord->setStyleSheet("background-color: #B91C1C; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; font-size: 10pt;");

        double avgCn = 0.0;
        double avgPow = 0.0;
        if (!m_currentRecCn.isEmpty()) {
            double sumCn = std::accumulate(m_currentRecCn.begin(), m_currentRecCn.end(), 0.0);
            avgCn = sumCn / m_currentRecCn.size();
        }
        if (!m_currentRecPow.isEmpty()) {
            double sumPow = std::accumulate(m_currentRecPow.begin(), m_currentRecPow.end(), 0.0);
            avgPow = sumPow / m_currentRecPow.size();
        }

        QString curveStr;
        for (int i = 0; i < m_currentRecCn.size(); ++i) {
            curveStr += QString::number(m_currentRecCn[i], 'f', 2) + ",";
        }
        if (curveStr.endsWith(',')) curveStr.chop(1);

        GoldenFrequencyRecord rec;
        rec.id = 0;
        rec.label = QString("Canlı Ölçüm #%1").arg(m_records.size() + 1);
        rec.frequencyMhz = 11054 + (m_records.size() * 10);
        rec.symbolRate = 30000;
        rec.polarization = "V";
        rec.goldenCnDb = avgCn;
        rec.goldenPowDbm = avgPow;
        rec.toleranceDb = 1.50;
        rec.updatedAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        rec.sampleData = curveStr;
        rec.durationSec = m_recordingElapsedSec;

        m_records.append(rec);
        DbManager::instance().saveGoldenFrequency(rec);

        int row = m_tableMeasurements->rowCount();
        m_tableMeasurements->insertRow(row);
        populateRow(row, rec);
        m_tableMeasurements->selectRow(row);

        m_lblLiveRecordStatus->setText(QString("✔ Kayıt tamamlandı! Süre: %1 sn, Örnek: %2 adet, Ort C/N: %3 dB")
                                           .arg(m_recordingElapsedSec)
                                           .arg(m_currentRecCn.size())
                                           .arg(avgCn, 0, 'f', 2));
        m_lblLiveRecordStatus->setStyleSheet("color: #34D399; font-weight: bold; padding-left: 8px;");

        emit logMessage(QString("[Golden Reference] Canlı kayıt kaydedildi: %1 (Ort C/N: %2 dB, %3 sn)")
                            .arg(rec.label).arg(avgCn, 0, 'f', 2).arg(m_recordingElapsedSec));
        updateMiniPlotForRecord(rec);
    }
}

void GoldenReferenceWidget::onRecordTick() {
    if (!m_isRecording) return;
    m_recordingElapsedSec++;
    int mm = m_recordingElapsedSec / 60;
    int ss = m_recordingElapsedSec % 60;

    double lastCn = m_currentRecCn.isEmpty() ? 0.0 : m_currentRecCn.last();
    double lastPow = m_currentRecPow.isEmpty() ? 0.0 : m_currentRecPow.last();

    m_lblLiveRecordStatus->setText(
        QString("🔴 KAYDEDİLİYOR... %1:%2 | Anlık C/N: %3 dB | Güç: %4 dBm | Örnek: %5")
            .arg(mm, 2, 10, QChar('0'))
            .arg(ss, 2, 10, QChar('0'))
            .arg(lastCn, 0, 'f', 1)
            .arg(lastPow, 0, 'f', 1)
            .arg(m_currentRecCn.size()));

    // Poll receiver actively during recording
    if (m_refWebClient && m_refWebClient->isReachable()) {
        m_refWebClient->requestStatus();
    } else if (m_refSerial && m_refSerial->isConnected()) {
        m_refSerial->requestStatus();
    }
}

void GoldenReferenceWidget::onAddFixedMeasurementClicked() {
    QString lbl = m_txtFixedLabel ? m_txtFixedLabel->text().trimmed() : QString();
    if (lbl.isEmpty()) {
        lbl = QString("Sabit Ölçüm #%1").arg(m_records.size() + 1);
    }

    GoldenFrequencyRecord rec;
    rec.id = 0;
    rec.label = lbl;
    rec.frequencyMhz = 11054 + (m_records.size() * 10);
    rec.symbolRate = 30000;
    rec.polarization = "V";
    rec.goldenCnDb = m_spinFixedCn ? m_spinFixedCn->value() : 14.50;
    rec.goldenPowDbm = m_spinFixedPow ? m_spinFixedPow->value() : -42.00;
    rec.toleranceDb = m_spinFixedTol ? m_spinFixedTol->value() : 1.50;
    rec.updatedAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    rec.durationSec = 0;

    // Create synthetic flat sample curve
    QString flatCurve;
    for (int i = 0; i < 15; ++i) {
        flatCurve += QString::number(rec.goldenCnDb, 'f', 2) + ",";
    }
    if (flatCurve.endsWith(',')) flatCurve.chop(1);
    rec.sampleData = flatCurve;

    m_records.append(rec);
    DbManager::instance().saveGoldenFrequency(rec);

    int row = m_tableMeasurements->rowCount();
    m_tableMeasurements->insertRow(row);
    populateRow(row, rec);
    m_tableMeasurements->selectRow(row);

    emit logMessage("[Golden Reference] Sabit referans ölçümü eklendi: " + rec.label);
    updateMiniPlotForRecord(rec);
}

void GoldenReferenceWidget::onRemoveSelectedRowClicked() {
    int row = m_tableMeasurements->currentRow();
    if (row < 0 || row >= m_tableMeasurements->rowCount()) return;

    if (auto *it = m_tableMeasurements->item(row, 0)) {
        int id = it->data(Qt::UserRole).toInt();
        if (id > 0) {
            DbManager::instance().deleteGoldenFrequency(id);
        }
    }

    if (row < m_records.size()) {
        m_records.removeAt(row);
    }
    m_tableMeasurements->removeRow(row);

    for (int r = 0; r < m_tableMeasurements->rowCount(); ++r) {
        if (auto *it = m_tableMeasurements->item(r, 0)) {
            it->setText(QString::number(r + 1));
        }
    }

    if (m_tableMeasurements->rowCount() > 0) {
        m_tableMeasurements->selectRow(qMin(row, m_tableMeasurements->rowCount() - 1));
    } else {
        m_miniPlot->graph(0)->data()->clear();
        m_miniPlot->graph(1)->data()->clear();
        m_miniPlot->graph(2)->data()->clear();
        m_miniPlot->graph(3)->data()->clear();
        m_miniPlot->replot();
        m_lblPlotTitle->setText("📊 Seçili Ölçüm Analiz Grafiği: (Ölçüm bulunmuyor)");
    }
}

void GoldenReferenceWidget::onSaveAllClicked() {
    for (int r = 0; r < m_tableMeasurements->rowCount(); ++r) {
        GoldenFrequencyRecord rec = readRow(r);
        DbManager::instance().saveGoldenFrequency(rec);
        if (r < m_records.size()) {
            m_records[r] = rec;
        }
    }
    QMessageBox::information(this, "Kaydedildi", "Tüm Golden Reference ölçüm ve tolerans kayıtları veritabanına aktarıldı.");
    emit logMessage("[Golden Reference] Tüm ölçüm kayıtları veritabanına kaydedildi.");
}

void GoldenReferenceWidget::onTableSelectionChanged() {
    int row = m_tableMeasurements->currentRow();
    if (row < 0 || row >= m_tableMeasurements->rowCount()) return;
    GoldenFrequencyRecord rec = readRow(row);
    updateMiniPlotForRecord(rec);
}

void GoldenReferenceWidget::updateMiniPlotForRecord(const GoldenFrequencyRecord &rec) {
    if (!m_miniPlot) return;

    m_lblPlotTitle->setText(QString("📊 Seçili Ölçüm Analiz Grafiği: %1  |  Ort. C/N: %2 dB  |  Güç: %3 dBm  |  Tolerans: ±%4 dB")
                                .arg(rec.label)
                                .arg(rec.goldenCnDb, 0, 'f', 2)
                                .arg(rec.goldenPowDbm, 0, 'f', 2)
                                .arg(rec.toleranceDb, 0, 'f', 2));

    QVector<double> xKeys, yValues;
    if (!rec.sampleData.isEmpty()) {
        const QStringList tokens = rec.sampleData.split(',', Qt::SkipEmptyParts);
        for (int i = 0; i < tokens.size(); ++i) {
            xKeys.append(i + 1);
            yValues.append(tokens[i].trimmed().toDouble());
        }
    }

    if (xKeys.isEmpty()) {
        for (int i = 1; i <= 10; ++i) {
            xKeys.append(i);
            yValues.append(rec.goldenCnDb);
        }
    }

    QVector<double> yAvg(xKeys.size(), rec.goldenCnDb);
    QVector<double> yUpper(xKeys.size(), rec.goldenCnDb + rec.toleranceDb);
    QVector<double> yLower(xKeys.size(), qMax(0.0, rec.goldenCnDb - rec.toleranceDb));

    m_miniPlot->xAxis->setRange(0.5, qMax(10.0, static_cast<double>(xKeys.size()) + 0.5));
    double maxVal = qMax(rec.goldenCnDb + rec.toleranceDb, 15.0);
    m_miniPlot->yAxis->setRange(0.0, maxVal + 2.5);

    m_miniPlot->graph(0)->setData(xKeys, yValues);
    m_miniPlot->graph(1)->setData(xKeys, yAvg);
    m_miniPlot->graph(2)->setData(xKeys, yUpper);
    m_miniPlot->graph(3)->setData(xKeys, yLower);
    m_miniPlot->replot();
}

void GoldenReferenceWidget::populateRow(int row, const GoldenFrequencyRecord &rec) {
    m_tableMeasurements->setRowHeight(row, 34);

    auto *itNo = new QTableWidgetItem(QString::number(row + 1));
    itNo->setTextAlignment(Qt::AlignCenter);
    itNo->setFlags(itNo->flags() & ~Qt::ItemIsEditable);
    itNo->setData(Qt::UserRole, rec.id);
    m_tableMeasurements->setItem(row, 0, itNo);

    m_tableMeasurements->setItem(row, 1, new QTableWidgetItem(rec.label));

    // Ortalama C/N
    auto *spinCn = new QDoubleSpinBox(m_tableMeasurements);
    spinCn->setRange(0.0, 35.0);
    spinCn->setDecimals(2);
    spinCn->setSuffix(" dB");
    spinCn->setValue(rec.goldenCnDb);
    spinCn->setAlignment(Qt::AlignCenter);
    connect(spinCn, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, row](double val) {
        if (row < m_records.size()) m_records[row].goldenCnDb = val;
        onTableSelectionChanged();
    });
    m_tableMeasurements->setCellWidget(row, 2, spinCn);

    // Ortalama Güç
    auto *spinPow = new QDoubleSpinBox(m_tableMeasurements);
    spinPow->setRange(-120.0, 10.0);
    spinPow->setDecimals(2);
    spinPow->setSuffix(" dBm");
    spinPow->setValue(rec.goldenPowDbm);
    spinPow->setAlignment(Qt::AlignCenter);
    connect(spinPow, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, row](double val) {
        if (row < m_records.size()) m_records[row].goldenPowDbm = val;
    });
    m_tableMeasurements->setCellWidget(row, 3, spinPow);

    // Tolerans
    auto *spinTol = new QDoubleSpinBox(m_tableMeasurements);
    spinTol->setRange(0.10, 10.0);
    spinTol->setDecimals(2);
    spinTol->setSuffix(" dB");
    spinTol->setValue(rec.toleranceDb);
    spinTol->setAlignment(Qt::AlignCenter);
    connect(spinTol, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, row](double val) {
        if (row < m_records.size()) m_records[row].toleranceDb = val;
        onTableSelectionChanged();
    });
    m_tableMeasurements->setCellWidget(row, 4, spinTol);

    // Kayıt Türü & Süre
    QString typeStr = (rec.durationSec > 0) ? QString("🔴 Canlı (%1 sn)").arg(rec.durationSec) : "📌 Sabit Değer";
    auto *itType = new QTableWidgetItem(typeStr);
    itType->setTextAlignment(Qt::AlignCenter);
    itType->setFlags(itType->flags() & ~Qt::ItemIsEditable);
    itType->setForeground(QBrush(QColor(rec.durationSec > 0 ? "#F87171" : "#38BDF8")));
    m_tableMeasurements->setItem(row, 5, itType);

    // Tarih / Saat
    auto *itDate = new QTableWidgetItem(rec.updatedAt.isEmpty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") : rec.updatedAt);
    itDate->setTextAlignment(Qt::AlignCenter);
    itDate->setFlags(itDate->flags() & ~Qt::ItemIsEditable);
    m_tableMeasurements->setItem(row, 6, itDate);

    // Durum
    auto *itStatus = new QTableWidgetItem("✔ AKTİF");
    itStatus->setTextAlignment(Qt::AlignCenter);
    itStatus->setFlags(itStatus->flags() & ~Qt::ItemIsEditable);
    itStatus->setForeground(QBrush(QColor("#34D399")));
    m_tableMeasurements->setItem(row, 7, itStatus);
}

GoldenFrequencyRecord GoldenReferenceWidget::readRow(int row) const {
    GoldenFrequencyRecord rec;
    if (row < 0 || row >= m_tableMeasurements->rowCount()) return rec;

    if (row < m_records.size()) {
        rec = m_records[row];
    }

    if (auto *itNo = m_tableMeasurements->item(row, 0)) {
        rec.id = itNo->data(Qt::UserRole).toInt();
    }
    if (auto *itLbl = m_tableMeasurements->item(row, 1)) {
        rec.label = itLbl->text().trimmed();
    }
    if (auto *wCn = qobject_cast<QDoubleSpinBox*>(m_tableMeasurements->cellWidget(row, 2))) {
        rec.goldenCnDb = wCn->value();
    }
    if (auto *wPow = qobject_cast<QDoubleSpinBox*>(m_tableMeasurements->cellWidget(row, 3))) {
        rec.goldenPowDbm = wPow->value();
    }
    if (auto *wTol = qobject_cast<QDoubleSpinBox*>(m_tableMeasurements->cellWidget(row, 4))) {
        rec.toleranceDb = wTol->value();
    }
    return rec;
}

void GoldenReferenceWidget::reloadFrequencies() {
    m_records = DbManager::instance().getGoldenFrequencies();
    m_tableMeasurements->setRowCount(m_records.size());
    for (int i = 0; i < m_records.size(); ++i) {
        populateRow(i, m_records[i]);
    }
    if (!m_records.isEmpty()) {
        m_tableMeasurements->selectRow(0);
        updateMiniPlotForRecord(m_records.first());
    }
}
