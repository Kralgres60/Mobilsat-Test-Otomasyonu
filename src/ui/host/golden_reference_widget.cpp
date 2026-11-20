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
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <numeric>

GoldenReferenceWidget::GoldenReferenceWidget(DataStream *refStream, QWidget *parent)
    : QWidget(parent),
      m_refStream(refStream)
{
    setupUi();

    m_recordTimer.setInterval(1000);
    connect(&m_recordTimer, &QTimer::timeout, this, &GoldenReferenceWidget::onRecordTick);

    if (m_refStream) {
        connect(m_refStream, &DataStream::telemetryReceived, this,
                [this](double powDbm, double cnDb, double, double) {
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
        "Kayıtlı Golden Sample modunda hangi ölçümün istasyonlarda otomatik seçileceğini belirleyin:",
        grpRefMode);
    lblModeInfo->setWordWrap(true);
    lblModeInfo->setStyleSheet("color: #A7C4B5;");

    m_comboRefMode = new QComboBox(grpRefMode);
    m_comboRefMode->addItem("🟢 Canlı Referans Anten (Live Reference)", static_cast<int>(ReferenceMode::LiveReference));
    m_comboRefMode->addItem("🌟 Kayıtlı Golden Sample (Referans Anten Yokken)", static_cast<int>(ReferenceMode::GoldenSample));
    m_comboRefMode->setMinimumWidth(280);

    QPushButton *btnApplyMode = new QPushButton("✅ Modu İstasyonlara Bildir", grpRefMode);
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

    // Row 0: Target Transponder Parameters (Frequency, Symbol Rate, Polarization)
    QHBoxLayout *rowTarget = new QHBoxLayout();
    rowTarget->setSpacing(8);
    QLabel *lblTargetTitle = new QLabel("🎯 Hedef Transponder:", grpControls);
    lblTargetTitle->setStyleSheet("font-weight: bold; color: #FBBF24;");
    rowTarget->addWidget(lblTargetTitle);

    rowTarget->addWidget(new QLabel("Frekans:", grpControls));
    m_spinTargetFreq = new QSpinBox(grpControls);
    m_spinTargetFreq->setRange(950, 14500);
    m_spinTargetFreq->setValue(12000);
    m_spinTargetFreq->setSuffix(" MHz");
    m_spinTargetFreq->setFixedWidth(115);
    rowTarget->addWidget(m_spinTargetFreq);

    rowTarget->addWidget(new QLabel("Sembol Oranı:", grpControls));
    m_spinTargetSr = new QSpinBox(grpControls);
    m_spinTargetSr->setRange(1000, 45000);
    m_spinTargetSr->setValue(30000);
    m_spinTargetSr->setSuffix(" kS/s");
    m_spinTargetSr->setFixedWidth(115);
    rowTarget->addWidget(m_spinTargetSr);

    rowTarget->addWidget(new QLabel("Pol:", grpControls));
    m_comboTargetPol = new QComboBox(grpControls);
    m_comboTargetPol->addItem("H");
    m_comboTargetPol->addItem("V");
    m_comboTargetPol->setCurrentText("H");
    m_comboTargetPol->setFixedWidth(65);
    rowTarget->addWidget(m_comboTargetPol);

    rowTarget->addStretch();
    ctrlVBox->addLayout(rowTarget);

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
    m_tableMeasurements->setColumnCount(11);
    m_tableMeasurements->setHorizontalHeaderLabels({
        "#", "Ölçüm Adı / Etiket", "Frekans", "Sembol Oranı", "Pol", "Ortalama C/N", "Ortalama Güç", "Tolerans", "Kayıt Türü & Süre", "Tarih / Saat", "Aktif Referans Seçimi"
    });
    m_tableMeasurements->verticalHeader()->setVisible(false);
    m_tableMeasurements->setAlternatingRowColors(true);
    m_tableMeasurements->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableMeasurements->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableMeasurements->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableMeasurements->setColumnWidth(0, 36);
    m_tableMeasurements->setColumnWidth(2, 125);
    m_tableMeasurements->setColumnWidth(3, 130);
    m_tableMeasurements->setColumnWidth(4, 65);
    m_tableMeasurements->setColumnWidth(5, 115);
    m_tableMeasurements->setColumnWidth(6, 120);
    m_tableMeasurements->setColumnWidth(7, 100);
    m_tableMeasurements->setColumnWidth(8, 135);
    m_tableMeasurements->setColumnWidth(9, 145);
    m_tableMeasurements->setColumnWidth(10, 165);
    m_tableMeasurements->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableMeasurements, &QTableWidget::customContextMenuRequested, this, &GoldenReferenceWidget::showTableContextMenu);
    connect(m_tableMeasurements, &QTableWidget::itemSelectionChanged, this, &GoldenReferenceWidget::onTableSelectionChanged);
    connect(m_tableMeasurements, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!m_isPopulating && item && item->column() == 1) {
            autoSaveRow(item->row());
        }
    });
    m_tableMeasurements->installEventFilter(this);
    m_tableMeasurements->viewport()->installEventFilter(this);
    this->installEventFilter(this);
    tableVBox->addWidget(m_tableMeasurements, 1);

    // Visual Mini Graph (hidden by default; opens when a frequency row is clicked, collapses on empty click)
    m_lblPlotTitle = new QLabel("📊 Seçili Ölçüm Analiz Grafiği: (Grafiği görmek için tablodan bir ölçüme tıklayınız)", grpTable);
    m_lblPlotTitle->setStyleSheet("color: #89DCEB; font-weight: bold; margin-top: 4px;");
    m_lblPlotTitle->setVisible(false);
    tableVBox->addWidget(m_lblPlotTitle);

    m_miniPlot = new QCustomPlot(grpTable);
    setupMiniPlot();
    m_miniPlot->installEventFilter(this);
    m_miniPlot->setVisible(false);
    tableVBox->addWidget(m_miniPlot);

    m_lblStatus = new QLabel(
        "İpucu: Değişiklikler otomatik kaydedilir. Satır silmek için Sağ Tık veya Delete tuşunu kullanabilirsiniz. "
        "Grafikte yatay yakınlaştırma için Scroll, dikey ölçekleme için Shift + Scroll kullanınız.",
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

    // Enable horizontal mouse-wheel zoom and horizontal drag panning
    m_miniPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_miniPlot->axisRect()->setRangeDrag(Qt::Horizontal);
    m_miniPlot->axisRect()->setRangeZoom(Qt::Horizontal);
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

void GoldenReferenceWidget::setActiveGoldenRecord(int recordId, bool broadcastIfGoldenMode) {
    if (recordId <= 0) return;
    m_activeGoldenId = recordId;
    DbManager::instance().setActiveGoldenRecordId(m_activeGoldenId);
    DbManager::instance().setGoldenFrequencyActive(recordId, true);
    m_records = DbManager::instance().getGoldenFrequencies();
    refreshActiveGoldenUi();

    QString activeLbl = QString("#%1").arg(recordId);
    for (const auto &r : m_records) {
        if (r.id == recordId) {
            activeLbl = QString("'%1' (%2 MHz %3 — %4 dB)").arg(r.label).arg(r.frequencyMhz).arg(r.polarization).arg(r.goldenCnDb, 0, 'f', 1);
            break;
        }
    }

    emit logMessage(QString("[Golden Reference] Frekansa özel aktif Golden Referans seçildi: %1").arg(activeLbl));

    if (broadcastIfGoldenMode && m_comboRefMode) {
        ReferenceMode curMode = static_cast<ReferenceMode>(m_comboRefMode->currentData().toInt());
        if (curMode == ReferenceMode::GoldenSample) {
            emit referenceModeApplyRequested(ReferenceMode::GoldenSample);
        }
    }
}

void GoldenReferenceWidget::refreshActiveGoldenUi() {
    // Update table column 10 action buttons (per-frequency Active/Passive toggle)
    if (!m_tableMeasurements) return;
    for (int row = 0; row < m_tableMeasurements->rowCount() && row < m_records.size(); ++row) {
        const auto &rec = m_records[row];
        bool isAct = rec.isActive;
        auto *btnAct = qobject_cast<QPushButton*>(m_tableMeasurements->cellWidget(row, 10));
        if (!btnAct) {
            btnAct = new QPushButton(m_tableMeasurements);
            btnAct->setCursor(Qt::PointingHandCursor);
            m_tableMeasurements->setCellWidget(row, 10, btnAct);
        }
        btnAct->disconnect();
        int recId = rec.id;
        int recFreq = rec.frequencyMhz;
        QString recPol = rec.polarization;
        connect(btnAct, &QPushButton::clicked, this, [this, recId, isAct, recFreq, recPol]() {
            bool newState = !isAct;
            DbManager::instance().setGoldenFrequencyActive(recId, newState);
            if (newState) {
                m_activeGoldenId = recId;
                DbManager::instance().setActiveGoldenRecordId(m_activeGoldenId);
            }
            m_records = DbManager::instance().getGoldenFrequencies();
            refreshActiveGoldenUi();
            emit logMessage(QString("[Golden Reference] %1 MHz %2 kaydı %3 durumuna alındı.")
                                .arg(recFreq).arg(recPol).arg(newState ? "AKTİF" : "PASİF (Canlı Fallback)"));
            if (m_comboRefMode) {
                ReferenceMode curMode = static_cast<ReferenceMode>(m_comboRefMode->currentData().toInt());
                if (curMode == ReferenceMode::GoldenSample) {
                    emit referenceModeApplyRequested(ReferenceMode::GoldenSample);
                }
            }
        });

        if (isAct) {
            btnAct->setText("⭐ AKTİF (Pasif Yap)");
            btnAct->setStyleSheet("background-color: #F59E0B; color: #071210; border: 1px solid #FBBF24; border-radius: 4px; font-weight: bold; padding: 3px 6px;");
        } else {
            btnAct->setText("⚪ PASİF (Aktif Yap)");
            btnAct->setStyleSheet("background-color: #1E293B; color: #94A3B8; border: 1px solid #475569; border-radius: 4px; padding: 3px 6px;");
        }
    }
}

void GoldenReferenceWidget::onReferenceTelemetryReceived(double powDbm, double cnDb) {
    if (!m_isRecording) return;

    m_currentRecCn.append(cnDb);
    m_currentRecPow.append(powDbm);
    m_currentRecTime.append(m_recordingElapsedSec + (m_currentRecCn.size() % 10) * 0.1);

    // Live update mini plot during recording with automatic vertical scaling
    if (m_miniPlot && !m_currentRecCn.isEmpty()) {
        m_miniPlot->xAxis->setRange(0.0, qMax(10.0, m_currentRecTime.last() + 2.0));
        m_miniPlot->graph(0)->setData(m_currentRecTime, m_currentRecCn);
        m_miniPlot->yAxis->rescale(true);
        double c = m_miniPlot->yAxis->range().center();
        double s = qMax(2.0, m_miniPlot->yAxis->range().size() * 1.25);
        m_miniPlot->yAxis->setRange(qMax(0.0, c - s / 2.0), c + s / 2.0);
        m_miniPlot->replot();
    }
}

void GoldenReferenceWidget::onToggleLiveRecordClicked() {
    if (!m_isRecording) {
        // Start Audio-Style Live Recording
        const bool refReady = (m_refStream && m_refStream->isConnected());
        if (!refReady) {
            QMessageBox::warning(this, "Referans Alıcı Bağlı Değil",
                                 "Canlı referans kaydı yapabilmek için Referans DRX9000 alıcısının bağlı olması gerekir.\n\n"
                                 "İpucu: Alıcı bağlı değilse aşağıdaki 'Sabit Değer Kaydet' panelinden sabit referans değeri girebilirsiniz.");
            return;
        }

        int targetFreq = m_spinTargetFreq ? m_spinTargetFreq->value() : 12000;
        int targetSr = m_spinTargetSr ? m_spinTargetSr->value() : 30000;
        QString targetPol = m_comboTargetPol ? m_comboTargetPol->currentText() : "H";

        // Tune reference DRX and trigger immediate status poll via DataStream
        m_refStream->sendTune(targetFreq, targetSr, targetPol);
        m_refStream->requestStatus();

        m_isRecording = true;
        m_recordingElapsedSec = 0;
        m_currentRecCn.clear();
        m_currentRecPow.clear();
        m_currentRecTime.clear();

        m_btnLiveRecord->setText("⏹️ Kaydı Bitir & Listeye Ekle");
        m_btnLiveRecord->setStyleSheet("background-color: #DC2626; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold; font-size: 10pt;");
        m_lblLiveRecordStatus->setText(QString("🔴 KAYDEDİLİYOR... (%1 MHz %2) 00:00 sn | Referans alıcıdan telemetri alınıyor...")
                                           .arg(targetFreq).arg(targetPol));
        m_lblLiveRecordStatus->setStyleSheet("color: #F87171; font-weight: bold; padding-left: 8px;");

        m_lblPlotTitle->setText(QString("📊 Canlı Kaydedilen Referans Ölçüm Eğrisi (%1 MHz %2)").arg(targetFreq).arg(targetPol));
        m_lblPlotTitle->setVisible(true);
        m_miniPlot->setVisible(true);
        m_miniPlot->graph(0)->data()->clear();
        m_miniPlot->graph(1)->data()->clear();
        m_miniPlot->graph(2)->data()->clear();
        m_miniPlot->graph(3)->data()->clear();
        m_miniPlot->replot();

        m_recordTimer.start();
        emit logMessage(QString("[Golden Reference] %1 MHz %2 için canlı telemetri kaydı başlatıldı.")
                            .arg(targetFreq).arg(targetPol));
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

        int targetFreq = m_spinTargetFreq ? m_spinTargetFreq->value() : 12000;
        int targetSr = m_spinTargetSr ? m_spinTargetSr->value() : 30000;
        QString targetPol = m_comboTargetPol ? m_comboTargetPol->currentText() : "H";

        GoldenFrequencyRecord rec;
        rec.id = 0;
        rec.label = QString("Canlı Ölçüm (%1 MHz %2)").arg(targetFreq).arg(targetPol);
        rec.frequencyMhz = targetFreq;
        rec.symbolRate = targetSr;
        rec.polarization = targetPol;
        rec.goldenCnDb = avgCn;
        rec.goldenPowDbm = avgPow;
        rec.toleranceDb = 1.50;
        rec.isActive = true;
        rec.updatedAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        rec.sampleData = curveStr;
        rec.durationSec = qMax(1, m_recordingElapsedSec);

        if (!DbManager::instance().saveGoldenFrequency(rec)) {
            QString err = DbManager::instance().lastError();
            m_lblLiveRecordStatus->setText("❌ Kayıt başarısız! Veritabanı hatası: " + err);
            m_lblLiveRecordStatus->setStyleSheet("color: #EF4444; font-weight: bold; padding-left: 8px;");
            QMessageBox::warning(this, "Canlı Kayıt Hatası", "Canlı ölçüm veritabanına kaydedilemedi:\n" + err);
            return;
        }

        reloadFrequencies();
        if (!m_records.isEmpty()) {
            setActiveGoldenRecord(m_records.first().id, false);
        }

        m_lblLiveRecordStatus->setText(QString("✔ Kayıt tamamlandı ve Aktif Referans yapıldı! Frekans: %1 MHz, Süre: %2 sn, Örnek: %3, Ort C/N: %4 dB")
                                           .arg(targetFreq)
                                           .arg(rec.durationSec)
                                           .arg(m_currentRecCn.size())
                                           .arg(avgCn, 0, 'f', 2));
        m_lblLiveRecordStatus->setStyleSheet("color: #34D399; font-weight: bold; padding-left: 8px;");

        emit logMessage(QString("[Golden Reference] Canlı kayıt kaydedildi: %1 (Ort C/N: %2 dB, %3 sn, %4 örnek)")
                            .arg(rec.label).arg(avgCn, 0, 'f', 2).arg(rec.durationSec).arg(m_currentRecCn.size()));
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
    if (m_refStream && m_refStream->isConnected()) {
        m_refStream->requestStatus();
    }
}

void GoldenReferenceWidget::onAddFixedMeasurementClicked() {
    int targetFreq = m_spinTargetFreq ? m_spinTargetFreq->value() : 12000;
    int targetSr = m_spinTargetSr ? m_spinTargetSr->value() : 30000;
    QString targetPol = m_comboTargetPol ? m_comboTargetPol->currentText() : "H";

    QString lbl = m_txtFixedLabel ? m_txtFixedLabel->text().trimmed() : QString();
    if (lbl.isEmpty() || lbl == "Sabit Referans - 14.5 dB") {
        lbl = QString("Sabit Ölçüm (%1 MHz %2)").arg(targetFreq).arg(targetPol);
    }

    GoldenFrequencyRecord rec;
    rec.id = 0;
    rec.label = lbl;
    rec.frequencyMhz = targetFreq;
    rec.symbolRate = targetSr;
    rec.polarization = targetPol;
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

    DbManager::instance().saveGoldenFrequency(rec);
    reloadFrequencies();
    if (!m_records.isEmpty()) {
        setActiveGoldenRecord(m_records.first().id, false);
    }

    emit logMessage(QString("[Golden Reference] %1 MHz %2 için sabit referans eklendi: %3 (C/N: %4 dB, Tol: ±%5 dB)")
                        .arg(targetFreq).arg(targetPol).arg(rec.label).arg(rec.goldenCnDb, 0, 'f', 2).arg(rec.toleranceDb, 0, 'f', 2));
}

void GoldenReferenceWidget::onRemoveSelectedRowClicked() {
    if (!m_tableMeasurements) return;
    QVector<int> rowsToDelete;
    if (m_tableMeasurements->selectionModel()) {
        const auto selRows = m_tableMeasurements->selectionModel()->selectedRows();
        for (const QModelIndex &rIdx : selRows) {
            rowsToDelete.append(rIdx.row());
        }
    }
    if (rowsToDelete.isEmpty()) {
        int row = m_tableMeasurements->currentRow();
        if (row >= 0 && row < m_tableMeasurements->rowCount()) {
            rowsToDelete.append(row);
        }
    }
    if (rowsToDelete.isEmpty()) return;

    for (int row : rowsToDelete) {
        if (auto *it = m_tableMeasurements->item(row, 0)) {
            int deletedId = it->data(Qt::UserRole).toInt();
            if (deletedId > 0) {
                DbManager::instance().deleteGoldenFrequency(deletedId);
            }
        }
    }

    reloadFrequencies();
}

void GoldenReferenceWidget::autoSaveRow(int row) {
    if (m_isPopulating || row < 0 || row >= m_tableMeasurements->rowCount()) return;
    GoldenFrequencyRecord rec = readRow(row);
    if (DbManager::instance().saveGoldenFrequency(rec)) {
        if (row < m_records.size()) {
            m_records[row] = rec;
        }
        refreshActiveGoldenUi();
    }
}

void GoldenReferenceWidget::showTableContextMenu(const QPoint &pos) {
    if (!m_tableMeasurements) return;
    QModelIndex idx = m_tableMeasurements->indexAt(pos);
    if (!idx.isValid()) return;

    int row = idx.row();
    if (m_tableMeasurements->selectionModel() &&
        !m_tableMeasurements->selectionModel()->isRowSelected(row, QModelIndex())) {
        m_tableMeasurements->selectRow(row);
    }

    int selCount = m_tableMeasurements->selectionModel()
                       ? m_tableMeasurements->selectionModel()->selectedRows().size()
                       : 1;
    QMenu menu(this);
    QAction *actDelete = menu.addAction(selCount > 1
                                            ? QString("🗑️ Seçili %1 Frekansı Sil (Delete)").arg(selCount)
                                            : QString("🗑️ Seçili Frekansı Sil (Delete)"));
    QAction *actToggle = nullptr;
    if (selCount == 1 && row >= 0 && row < m_records.size()) {
        actToggle = menu.addAction(m_records[row].isActive ? "⚪ Pasif Yap (Canlı Teste Geç)" : "⭐ Aktif Yap");
    }

    QAction *chosen = menu.exec(m_tableMeasurements->viewport()->mapToGlobal(pos));
    if (chosen == actDelete) {
        onRemoveSelectedRowClicked();
    } else if (actToggle && chosen == actToggle && row < m_records.size()) {
        bool newState = !m_records[row].isActive;
        int recId = m_records[row].id;
        DbManager::instance().setGoldenFrequencyActive(recId, newState);
        if (newState) {
            m_activeGoldenId = recId;
            DbManager::instance().setActiveGoldenRecordId(m_activeGoldenId);
        }
        m_records = DbManager::instance().getGoldenFrequencies();
        refreshActiveGoldenUi();
    }
}

void GoldenReferenceWidget::onTableSelectionChanged() {
    if (!m_tableMeasurements) return;
    int row = m_tableMeasurements->currentRow();
    if (row < 0 || row >= m_tableMeasurements->rowCount() || m_tableMeasurements->selectedItems().isEmpty()) {
        if (!m_isRecording) {
            if (m_lblPlotTitle) m_lblPlotTitle->setVisible(false);
            if (m_miniPlot) m_miniPlot->setVisible(false);
        }
        return;
    }

    if (m_lblPlotTitle) m_lblPlotTitle->setVisible(true);
    if (m_miniPlot) m_miniPlot->setVisible(true);
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

    // Automatic vertical scaling around data and tolerance bounds
    double minVal = rec.goldenCnDb - rec.toleranceDb;
    double maxVal = rec.goldenCnDb + rec.toleranceDb;
    for (double y : yValues) {
        if (y < minVal) minVal = y;
        if (y > maxVal) maxVal = y;
    }
    double pad = qMax(0.5, (maxVal - minVal) * 0.15);
    m_miniPlot->yAxis->setRange(qMax(0.0, minVal - pad), maxVal + pad);

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

    // Frekans (MHz)
    auto *spinFreq = new QSpinBox(m_tableMeasurements);
    spinFreq->setRange(950, 14500);
    spinFreq->setValue(rec.frequencyMhz > 0 ? rec.frequencyMhz : 12000);
    spinFreq->setSuffix(" MHz");
    spinFreq->setAlignment(Qt::AlignCenter);
    connect(spinFreq, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, row](int val) {
        if (row < m_records.size()) m_records[row].frequencyMhz = val;
        autoSaveRow(row);
    });
    m_tableMeasurements->setCellWidget(row, 2, spinFreq);

    // Sembol Oranı (kS/s)
    auto *spinSr = new QSpinBox(m_tableMeasurements);
    spinSr->setRange(1000, 45000);
    spinSr->setValue(rec.symbolRate > 0 ? rec.symbolRate : 30000);
    spinSr->setSuffix(" kS/s");
    spinSr->setAlignment(Qt::AlignCenter);
    connect(spinSr, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, row](int val) {
        if (row < m_records.size()) m_records[row].symbolRate = val;
        autoSaveRow(row);
    });
    m_tableMeasurements->setCellWidget(row, 3, spinSr);

    // Polarizasyon (H/V)
    auto *comboPol = new QComboBox(m_tableMeasurements);
    comboPol->addItem("H");
    comboPol->addItem("V");
    comboPol->setCurrentText(rec.polarization.isEmpty() ? "H" : rec.polarization.toUpper());
    connect(comboPol, &QComboBox::currentTextChanged, this, [this, row](const QString &val) {
        if (row < m_records.size()) m_records[row].polarization = val;
        autoSaveRow(row);
    });
    m_tableMeasurements->setCellWidget(row, 4, comboPol);

    // Ortalama C/N
    auto *spinCn = new QDoubleSpinBox(m_tableMeasurements);
    spinCn->setRange(0.0, 35.0);
    spinCn->setDecimals(2);
    spinCn->setSuffix(" dB");
    spinCn->setValue(rec.goldenCnDb);
    spinCn->setAlignment(Qt::AlignCenter);
    connect(spinCn, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, row](double val) {
        if (row < m_records.size()) m_records[row].goldenCnDb = val;
        autoSaveRow(row);
        onTableSelectionChanged();
    });
    m_tableMeasurements->setCellWidget(row, 5, spinCn);

    // Ortalama Güç
    auto *spinPow = new QDoubleSpinBox(m_tableMeasurements);
    spinPow->setRange(-120.0, 10.0);
    spinPow->setDecimals(2);
    spinPow->setSuffix(" dBm");
    spinPow->setValue(rec.goldenPowDbm);
    spinPow->setAlignment(Qt::AlignCenter);
    connect(spinPow, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, row](double val) {
        if (row < m_records.size()) m_records[row].goldenPowDbm = val;
        autoSaveRow(row);
    });
    m_tableMeasurements->setCellWidget(row, 6, spinPow);

    // Tolerans
    auto *spinTol = new QDoubleSpinBox(m_tableMeasurements);
    spinTol->setRange(0.10, 10.0);
    spinTol->setDecimals(2);
    spinTol->setSuffix(" dB");
    spinTol->setValue(rec.toleranceDb);
    spinTol->setAlignment(Qt::AlignCenter);
    connect(spinTol, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, row](double val) {
        if (row < m_records.size()) m_records[row].toleranceDb = val;
        autoSaveRow(row);
        onTableSelectionChanged();
    });
    m_tableMeasurements->setCellWidget(row, 7, spinTol);

    // Kayıt Türü & Süre
    QString typeStr = (rec.durationSec > 0) ? QString("🔴 Canlı (%1 sn)").arg(rec.durationSec) : "📌 Sabit Değer";
    auto *itType = new QTableWidgetItem(typeStr);
    itType->setTextAlignment(Qt::AlignCenter);
    itType->setFlags(itType->flags() & ~Qt::ItemIsEditable);
    itType->setForeground(QBrush(QColor(rec.durationSec > 0 ? "#F87171" : "#38BDF8")));
    m_tableMeasurements->setItem(row, 8, itType);

    // Tarih / Saat
    auto *itDate = new QTableWidgetItem(rec.updatedAt.isEmpty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") : rec.updatedAt);
    itDate->setTextAlignment(Qt::AlignCenter);
    itDate->setFlags(itDate->flags() & ~Qt::ItemIsEditable);
    m_tableMeasurements->setItem(row, 9, itDate);

    // Aktif Seçim Butonu
    auto *btnAct = new QPushButton(m_tableMeasurements);
    btnAct->setCursor(Qt::PointingHandCursor);
    m_tableMeasurements->setCellWidget(row, 10, btnAct);
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
    if (auto *wFreq = qobject_cast<QSpinBox*>(m_tableMeasurements->cellWidget(row, 2))) {
        rec.frequencyMhz = wFreq->value();
    }
    if (auto *wSr = qobject_cast<QSpinBox*>(m_tableMeasurements->cellWidget(row, 3))) {
        rec.symbolRate = wSr->value();
    }
    if (auto *wPol = qobject_cast<QComboBox*>(m_tableMeasurements->cellWidget(row, 4))) {
        rec.polarization = wPol->currentText();
    }
    if (auto *wCn = qobject_cast<QDoubleSpinBox*>(m_tableMeasurements->cellWidget(row, 5))) {
        rec.goldenCnDb = wCn->value();
    }
    if (auto *wPow = qobject_cast<QDoubleSpinBox*>(m_tableMeasurements->cellWidget(row, 6))) {
        rec.goldenPowDbm = wPow->value();
    }
    if (auto *wTol = qobject_cast<QDoubleSpinBox*>(m_tableMeasurements->cellWidget(row, 7))) {
        rec.toleranceDb = wTol->value();
    }
    return rec;
}

void GoldenReferenceWidget::reloadFrequencies() {
    m_records = DbManager::instance().getGoldenFrequencies();
    m_activeGoldenId = DbManager::instance().getActiveGoldenRecordId();

    bool foundActive = false;
    for (const auto &r : m_records) {
        if (r.id == m_activeGoldenId && m_activeGoldenId > 0) {
            foundActive = true;
            break;
        }
    }
    if (!foundActive && !m_records.isEmpty()) {
        m_activeGoldenId = m_records.first().id;
        DbManager::instance().setActiveGoldenRecordId(m_activeGoldenId);
    }

    m_isPopulating = true;
    m_tableMeasurements->setRowCount(m_records.size());
    for (int i = 0; i < m_records.size(); ++i) {
        populateRow(i, m_records[i]);
    }
    refreshActiveGoldenUi();
    m_isPopulating = false;

    // Auto-fit columns horizontally to fit both header labels and cell widgets/data
    for (int c = 0; c < m_tableMeasurements->columnCount(); ++c) {
        if (c == 1) continue; // Column 1 stretches dynamically
        m_tableMeasurements->resizeColumnToContents(c);
        int w = m_tableMeasurements->columnWidth(c) + 18;
        if (c == 0) w = qMax(w, 36);
        else if (c == 2 || c == 3) w = qMax(w, 125);
        else if (c == 10) w = qMax(w, 165);
        m_tableMeasurements->setColumnWidth(c, w);
    }

    // Keep plot collapsed by default until the user clicks a frequency row
    m_tableMeasurements->clearSelection();
    m_tableMeasurements->setCurrentCell(-1, -1);
    if (!m_isRecording) {
        if (m_lblPlotTitle) m_lblPlotTitle->setVisible(false);
        if (m_miniPlot) m_miniPlot->setVisible(false);
    }
}

bool GoldenReferenceWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Delete && m_tableMeasurements &&
            (watched == m_tableMeasurements || watched == m_tableMeasurements->viewport())) {
            onRemoveSelectedRowClicked();
            return true;
        }
    } else if (event->type() == QEvent::Wheel && m_miniPlot && watched == m_miniPlot) {
        auto *we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ShiftModifier) {
            m_miniPlot->axisRect()->setRangeZoom(Qt::Vertical);
        } else {
            m_miniPlot->axisRect()->setRangeZoom(Qt::Horizontal);
        }
        return false; // Let QCustomPlot handle the wheel event with updated zoom orientation
    } else if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (m_miniPlot && watched == m_miniPlot) {
            if (me->modifiers() & Qt::ShiftModifier) {
                m_miniPlot->axisRect()->setRangeDrag(Qt::Vertical);
            } else {
                m_miniPlot->axisRect()->setRangeDrag(Qt::Horizontal);
            }
            return false;
        }
        if (m_tableMeasurements && watched == m_tableMeasurements->viewport()) {
            // Clicked on empty area below/around rows inside the table
            if (!m_tableMeasurements->indexAt(me->pos()).isValid()) {
                m_tableMeasurements->clearSelection();
                m_tableMeasurements->setCurrentCell(-1, -1);
                if (!m_isRecording) {
                    if (m_lblPlotTitle) m_lblPlotTitle->setVisible(false);
                    if (m_miniPlot) m_miniPlot->setVisible(false);
                }
            }
        } else if (watched == this) {
            // Clicked on empty widget background outside table and plot
            QWidget *child = childAt(me->pos());
            bool insideTable = (child && m_tableMeasurements && (child == m_tableMeasurements || m_tableMeasurements->isAncestorOf(child)));
            bool insidePlot = (child && m_miniPlot && (child == m_miniPlot || m_miniPlot->isAncestorOf(child)));
            if (!insideTable && !insidePlot) {
                if (m_tableMeasurements) {
                    m_tableMeasurements->clearSelection();
                    m_tableMeasurements->setCurrentCell(-1, -1);
                }
                if (!m_isRecording) {
                    if (m_lblPlotTitle) m_lblPlotTitle->setVisible(false);
                    if (m_miniPlot) m_miniPlot->setVisible(false);
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
