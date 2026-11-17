/**
 * @file spc_analysis_widget.cpp
 * @brief Statistical Process Control (SPC) visualization dashboard with Gaussian curve and Cp/Cpk metrics.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "spc_analysis_widget.h"
#include "database/db_manager.h"
#include "core/app_config.h"
#include "ui/common/style_helper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QFileInfo>

SpcAnalysisWidget::SpcAnalysisWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();
    refreshAnalysis();
}

void SpcAnalysisWidget::setupUi() {
    QVBoxLayout *mainVBox = new QVBoxLayout(this);
    mainVBox->setContentsMargins(8, 8, 8, 8);
    mainVBox->setSpacing(8);

    // 1. Top Filter Bar
    QFrame *filterFrame = new QFrame(this);
    filterFrame->setStyleSheet("background-color: #111827; border: 1px solid #1F2937; border-radius: 6px;");
    QHBoxLayout *filterLayout = new QHBoxLayout(filterFrame);
    filterLayout->setContentsMargins(10, 8, 10, 8);
    filterLayout->setSpacing(10);

    QLabel *lblIcon = new QLabel("📊", filterFrame);
    lblIcon->setStyleSheet("font-size: 14pt;");
    filterLayout->addWidget(lblIcon);

    filterLayout->addWidget(new QLabel("İş Emri:", filterFrame));
    m_comboWorkOrders = new QComboBox(filterFrame);
    m_comboWorkOrders->addItem("Tüm İş Emirleri", -1);
    m_comboWorkOrders->setMinimumWidth(130);
    filterLayout->addWidget(m_comboWorkOrders);

    filterLayout->addWidget(new QLabel("İstasyon:", filterFrame));
    m_comboStations = new QComboBox(filterFrame);
    m_comboStations->addItem("Tüm İstasyonlar", -1);
    m_comboStations->addItem("İstasyon 1", 1);
    m_comboStations->addItem("İstasyon 2", 2);
    m_comboStations->addItem("İstasyon 3", 3);
    filterLayout->addWidget(m_comboStations);

    filterLayout->addWidget(new QLabel("Uydu Profili:", filterFrame));
    m_comboProfiles = new QComboBox(filterFrame);
    m_comboProfiles->addItem("Tüm Profiller", -1);
    m_comboProfiles->setMinimumWidth(130);
    filterLayout->addWidget(m_comboProfiles);

    filterLayout->addWidget(new QLabel("Tolerans:", filterFrame));
    m_spinTol = new QDoubleSpinBox(filterFrame);
    m_spinTol->setRange(0.20, 10.00);
    m_spinTol->setSingleStep(0.10);
    m_spinTol->setValue(AppConfig::instance().defaultToleranceDb() > 0 ? AppConfig::instance().defaultToleranceDb() : 1.50);
    m_spinTol->setPrefix("±");
    m_spinTol->setSuffix(" dB");
    filterLayout->addWidget(m_spinTol);

    filterLayout->addWidget(new QLabel("Örnek:", filterFrame));
    m_comboLimit = new QComboBox(filterFrame);
    m_comboLimit->addItem("Son 50", 50);
    m_comboLimit->addItem("Son 100", 100);
    m_comboLimit->addItem("Son 250", 250);
    m_comboLimit->addItem("Son 500", 500);
    m_comboLimit->addItem("Son 1000", 1000);
    m_comboLimit->setCurrentIndex(1); // default 100
    filterLayout->addWidget(m_comboLimit);

    m_btnRefresh = new QPushButton("🔄 Analizi Güncelle", filterFrame);
    m_btnRefresh->setStyleSheet("background-color: #059669; color: white; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnRefresh, &QPushButton::clicked, this, &SpcAnalysisWidget::onFilterChanged);
    filterLayout->addWidget(m_btnRefresh);

    m_btnExportImg = new QPushButton("📷 Grafiği Kaydet", filterFrame);
    m_btnExportImg->setStyleSheet("background-color: #374151; color: white; border: 1px solid #4B5563; border-radius: 4px; padding: 6px 12px;");
    connect(m_btnExportImg, &QPushButton::clicked, this, &SpcAnalysisWidget::onExportImageClicked);
    filterLayout->addWidget(m_btnExportImg);

    filterLayout->addStretch();
    mainVBox->addWidget(filterFrame);

    // 2. Main Content Split: Graph (Left) & KPI Summary (Right)
    QHBoxLayout *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(10);

    // Left: QCustomPlot
    m_plot = new QCustomPlot(this);
    setupPlot();
    contentLayout->addWidget(m_plot, 7);

    // Right: KPI Panel
    QFrame *kpiFrame = new QFrame(this);
    kpiFrame->setStyleSheet("background-color: #111827; border: 1px solid #1F2937; border-radius: 6px; padding: 8px;");
    QVBoxLayout *kpiLayout = new QVBoxLayout(kpiFrame);
    kpiLayout->setContentsMargins(10, 10, 10, 10);
    kpiLayout->setSpacing(8);

    // Verdict Badge Frame
    m_verdictFrame = new QFrame(kpiFrame);
    m_verdictFrame->setStyleSheet("background-color: #064E3B; border: 2px solid #10B981; border-radius: 6px; padding: 10px;");
    QVBoxLayout *verdictLayout = new QVBoxLayout(m_verdictFrame);
    verdictLayout->setContentsMargins(6, 6, 6, 6);
    verdictLayout->setSpacing(3);

    m_lblVerdictBadge = new QLabel("HESAPLANIYOR...", m_verdictFrame);
    m_lblVerdictBadge->setAlignment(Qt::AlignCenter);
    m_lblVerdictBadge->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");
    verdictLayout->addWidget(m_lblVerdictBadge);

    m_lblVerdictText = new QLabel("Süreç yeterlilik analizi yapılıyor...", m_verdictFrame);
    m_lblVerdictText->setAlignment(Qt::AlignCenter);
    m_lblVerdictText->setWordWrap(true);
    m_lblVerdictText->setStyleSheet("font-size: 8pt; color: #E5E7EB;");
    verdictLayout->addWidget(m_lblVerdictText);

    kpiLayout->addWidget(m_verdictFrame);

    // KPI Metrics Form
    QGroupBox *grpMetrics = new QGroupBox("📐 İstatistik ve Yetenek Parametreleri", kpiFrame);
    QFormLayout *form = new QFormLayout(grpMetrics);
    form->setSpacing(6);

    auto makeValueLabel = [this, grpMetrics](const QString &color = "#F9FAFB") -> QLabel* {
        QLabel *lbl = new QLabel("--", grpMetrics);
        lbl->setStyleSheet(QString("font-weight: bold; color: %1; font-size: 9.5pt;").arg(color));
        return lbl;
    };

    m_lblSampleCount = makeValueLabel("#60A5FA");
    m_lblMean = makeValueLabel("#FBBF24");
    m_lblStdDev = makeValueLabel("#A78BFA");
    m_lblCp = makeValueLabel("#34D399");
    m_lblCpk = makeValueLabel("#34D399");
    m_lblThreeSigma = makeValueLabel("#F472B6");
    m_lblMinMax = makeValueLabel("#9CA3AF");
    m_lblToleranceLimits = makeValueLabel("#EF4444");

    form->addRow("Örneklem Sayısı (N):", m_lblSampleCount);
    form->addRow("Ortalama Sapma (μ):", m_lblMean);
    form->addRow("Standart Sapma (σ):", m_lblStdDev);
    form->addRow("Süreç Yeteneği (Cp):", m_lblCp);
    form->addRow("Süreç İndeksi (Cpk):", m_lblCpk);
    form->addRow("3-Sigma Sınırları [±3σ]:", m_lblThreeSigma);
    form->addRow("Gözlenen Min / Max:", m_lblMinMax);
    form->addRow("Tolerans Sınırları [LSL / USL]:", m_lblToleranceLimits);

    kpiLayout->addWidget(grpMetrics);

    // Reference guidelines note
    QLabel *lblGuide = new QLabel(
        "<b>SPC Rehberi:</b><br/>"
        "• <b>Cpk ≥ 1.33:</b> 4-Sigma Mükemmel kalite<br/>"
        "• <b>1.00 ≤ Cpk &lt; 1.33:</b> Kabul edilebilir, izlenmeli<br/>"
        "• <b>Cpk &lt; 1.00:</b> Yetersiz, tolerans dışına taşma riski!",
        kpiFrame);
    lblGuide->setStyleSheet("font-size: 7.5pt; color: #94A3B8; background-color: #0F172A; padding: 6px; border-radius: 4px;");
    kpiLayout->addWidget(lblGuide);

    kpiLayout->addStretch();
    contentLayout->addWidget(kpiFrame, 3);

    mainVBox->addLayout(contentLayout, 1);
}

void SpcAnalysisWidget::setupPlot() {
    m_plot->setBackground(QBrush(QColor("#0A1F17")));
    m_plot->xAxis->setBasePen(QPen(QColor("#2D5E45")));
    m_plot->yAxis->setBasePen(QPen(QColor("#2D5E45")));
    m_plot->xAxis->setTickPen(QPen(QColor("#4B7A66")));
    m_plot->yAxis->setTickPen(QPen(QColor("#4B7A66")));
    m_plot->xAxis->setTickLabelColor(QColor("#E6F4EE"));
    m_plot->yAxis->setTickLabelColor(QColor("#E6F4EE"));
    m_plot->xAxis->grid()->setPen(QPen(QColor("#1A3D2B"), 1, Qt::DotLine));
    m_plot->yAxis->grid()->setPen(QPen(QColor("#1A3D2B"), 1, Qt::DotLine));
    m_plot->xAxis->setLabel("Sinyal Farkı Δ (DUT - REF C/N) [dB]");
    m_plot->yAxis->setLabel("Frekans / Gözlem Sayısı");
    m_plot->xAxis->setLabelColor(QColor("#A7C4B5"));
    m_plot->yAxis->setLabelColor(QColor("#A7C4B5"));

    // Histogram Bars Plottable
    m_bars = new QCPBars(m_plot->xAxis, m_plot->yAxis);
    m_bars->setPen(QPen(QColor("#3B82F6"), 1.2));
    m_bars->setBrush(QBrush(QColor(59, 130, 246, 120)));
    m_bars->setName("Gözlem Sayısı (Histogram)");

    // Graph 0: Gaussian Bell Curve
    m_plot->addGraph();
    m_plot->graph(0)->setPen(QPen(QColor("#34D399"), 2.5));
    m_plot->graph(0)->setName("Gauss Normal Dağılım Çanı");

    // Graph 1: Upper Specification Limit (USL)
    m_plot->addGraph();
    m_plot->graph(1)->setPen(QPen(QColor("#EF4444"), 1.8, Qt::DashLine));
    m_plot->graph(1)->setName("Üst Tolerans (USL)");

    // Graph 2: Lower Specification Limit (LSL)
    m_plot->addGraph();
    m_plot->graph(2)->setPen(QPen(QColor("#EF4444"), 1.8, Qt::DashLine));
    m_plot->graph(2)->setName("Alt Tolerans (LSL)");

    // Graph 3: Mean (\mu) Tracer
    m_plot->addGraph();
    m_plot->graph(3)->setPen(QPen(QColor("#FBBF24"), 1.8, Qt::DotLine));
    m_plot->graph(3)->setName("Ortalama (μ)");

    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(10, 31, 23, 220)));
    m_plot->legend->setBorderPen(QPen(QColor("#1A3D2B")));
    m_plot->legend->setTextColor(QColor("#E6F4EE"));

    m_plot->xAxis->setRange(-2.5, 2.5);
    m_plot->yAxis->setRange(0, 15);
    m_plot->replot();
}

void SpcAnalysisWidget::refreshAnalysis() {
    // Refresh Work Orders combo
    int currentWo = m_comboWorkOrders->currentData().toInt();
    m_comboWorkOrders->blockSignals(true);
    m_comboWorkOrders->clear();
    m_comboWorkOrders->addItem("Tüm İş Emirleri", -1);
    QVector<WorkOrderInfo> orders = DbManager::instance().getWorkOrders();
    for (const auto &wo : orders) {
        m_comboWorkOrders->addItem(QString("%1 (%2)").arg(wo.orderNo, wo.customerName), wo.id);
    }
    int woIdx = m_comboWorkOrders->findData(currentWo);
    if (woIdx >= 0) m_comboWorkOrders->setCurrentIndex(woIdx);
    m_comboWorkOrders->blockSignals(false);

    // Refresh Profiles combo
    int currentProf = m_comboProfiles->currentData().toInt();
    m_comboProfiles->blockSignals(true);
    m_comboProfiles->clear();
    m_comboProfiles->addItem("Tüm Profiller", -1);
    QVector<SatelliteProfileInfo> profiles = DbManager::instance().getProfiles();
    for (const auto &p : profiles) {
        m_comboProfiles->addItem(p.profileName, p.id);
    }
    int profIdx = m_comboProfiles->findData(currentProf);
    if (profIdx >= 0) m_comboProfiles->setCurrentIndex(profIdx);
    m_comboProfiles->blockSignals(false);

    onFilterChanged();
}

void SpcAnalysisWidget::onFilterChanged() {
    int woId = m_comboWorkOrders->currentData().toInt();
    int stId = m_comboStations->currentData().toInt();
    int profId = m_comboProfiles->currentData().toInt();
    int limit = m_comboLimit->currentData().toInt();
    double tol = m_spinTol->value();

    QVector<double> data = DbManager::instance().getDeltaDbValuesForSpc(woId, stId, profId, limit);
    SpcResult spc = SpcCalculator::calculate(data, tol, 15);

    updateKpiDisplay(spc);
    renderSpcPlot(spc);
}

void SpcAnalysisWidget::updateKpiDisplay(const SpcResult &spc) {
    if (spc.sampleCount < 2) {
        m_verdictFrame->setStyleSheet("background-color: #1F2937; border: 2px solid #4B5563; border-radius: 6px; padding: 10px;");
        m_lblVerdictBadge->setText("YETERSİZ VERİ");
        m_lblVerdictBadge->setStyleSheet("font-size: 13pt; font-weight: bold; color: #9CA3AF;");
        m_lblVerdictText->setText("SPC analizi için en az 2 test kaydı gereklidir.");

        m_lblSampleCount->setText(QString::number(spc.sampleCount));
        m_lblMean->setText("--");
        m_lblStdDev->setText("--");
        m_lblCp->setText("--");
        m_lblCpk->setText("--");
        m_lblThreeSigma->setText("--");
        m_lblMinMax->setText("--");
        m_lblToleranceLimits->setText(QString("[-%1 / +%2 dB]").arg(spc.usl, 0, 'f', 2).arg(spc.usl, 0, 'f', 2));
        return;
    }

    // Set Verdict styling
    QString borderCol = spc.cpk >= 1.33 ? "#10B981" : (spc.cpk >= 1.00 ? "#F59E0B" : "#EF4444");
    QString bgCol = spc.cpk >= 1.33 ? "#064E3B" : (spc.cpk >= 1.00 ? "#451A03" : "#450A0A");
    QString textCol = spc.cpk >= 1.33 ? "#34D399" : (spc.cpk >= 1.00 ? "#FBBF24" : "#F87171");

    m_verdictFrame->setStyleSheet(QString("background-color: %1; border: 2px solid %2; border-radius: 6px; padding: 10px;").arg(bgCol, borderCol));
    m_lblVerdictBadge->setText(QString("Cpk = %1 — %2").arg(spc.cpk, 0, 'f', 2).arg(spc.ratingBadge));
    m_lblVerdictBadge->setStyleSheet(QString("font-size: 12pt; font-weight: bold; color: %1;").arg(textCol));
    m_lblVerdictText->setText(spc.ratingText);

    // Fill form metrics
    m_lblSampleCount->setText(QString("%1 Adet Test").arg(spc.sampleCount));
    QString meanSign = spc.mean > 0 ? "+" : "";
    m_lblMean->setText(QString("%1%2 dB").arg(meanSign).arg(spc.mean, 0, 'f', 3));
    m_lblStdDev->setText(QString("%1 dB").arg(spc.stdDev, 0, 'f', 3));
    m_lblCp->setText(QString::number(spc.cp, 'f', 2));
    m_lblCpk->setText(QString::number(spc.cpk, 'f', 2));
    m_lblThreeSigma->setText(QString("[%1 dB , %2 dB]").arg(spc.threeSigmaLower, 0, 'f', 2).arg(spc.threeSigmaUpper, 0, 'f', 2));
    m_lblMinMax->setText(QString("%1 dB / %2 dB").arg(spc.minVal, 0, 'f', 2).arg(spc.maxVal, 0, 'f', 2));
    m_lblToleranceLimits->setText(QString("[%1 dB , +%2 dB]").arg(spc.lsl, 0, 'f', 2).arg(spc.usl, 0, 'f', 2));
}

void SpcAnalysisWidget::renderSpcPlot(const SpcResult &spc) {
    if (!m_plot || !m_bars) return;

    if (spc.sampleCount < 2) {
        m_bars->data()->clear();
        m_plot->graph(0)->data()->clear();
        m_plot->graph(1)->data()->clear();
        m_plot->graph(2)->data()->clear();
        m_plot->graph(3)->data()->clear();
        m_plot->replot();
        return;
    }

    // 1. Histogram Bars
    m_bars->setWidth(spc.binWidth * 0.90);
    m_bars->setData(spc.binCenters, spc.binCounts);

    // 2. Gaussian Bell Curve
    m_plot->graph(0)->setData(spc.bellCurveX, spc.bellCurveY);

    // Calculate maximum Y for vertical specification lines
    double maxY = 5.0;
    for (double c : spc.binCounts) {
        if (c > maxY) maxY = c;
    }
    for (double y : spc.bellCurveY) {
        if (y > maxY) maxY = y;
    }
    maxY *= 1.25;

    // 3. USL line
    QVector<double> uslX = {spc.usl, spc.usl};
    QVector<double> uslY = {0.0, maxY};
    m_plot->graph(1)->setData(uslX, uslY);

    // 4. LSL line
    QVector<double> lslX = {spc.lsl, spc.lsl};
    QVector<double> lslY = {0.0, maxY};
    m_plot->graph(2)->setData(lslX, lslY);

    // 5. Mean tracer
    QVector<double> meanX = {spc.mean, spc.mean};
    QVector<double> meanY = {0.0, maxY};
    m_plot->graph(3)->setData(meanX, meanY);

    // Set Axis ranges with margin
    double minX = qMin(spc.lsl - 0.4, spc.minVal - 0.2);
    double maxX = qMax(spc.usl + 0.4, spc.maxVal + 0.2);
    m_plot->xAxis->setRange(minX, maxX);
    m_plot->yAxis->setRange(0, maxY);
    m_plot->replot();
}

void SpcAnalysisWidget::onExportImageClicked() {
    QString defaultName = QString("NETA_SPC_Analizi_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "SPC Grafiğini Kaydet",
        defaultName,
        "PNG Resim Dosyası (*.png);;PDF Belgesi (*.pdf);;Tüm Dosyalar (*.*)");

    if (fileName.isEmpty()) return;

    bool ok = false;
    if (fileName.endsWith(".pdf", Qt::CaseInsensitive)) {
        ok = m_plot->savePdf(fileName);
    } else {
        if (!fileName.endsWith(".png", Qt::CaseInsensitive)) fileName += ".png";
        ok = m_plot->savePng(fileName, 1200, 800);
    }

    if (ok) {
        emit logMessage(QString("📷 SPC grafiği kaydedildi: %1").arg(QFileInfo(fileName).fileName()));
        QMessageBox::information(this, "Kaydedildi", QString("SPC grafiği başarıyla kaydedildi:\n\n%1").arg(fileName));
    } else {
        QMessageBox::warning(this, "Hata", "Grafik dosyası kaydedilemedi.");
    }
}
