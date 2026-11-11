/**
 * @file golden_reference_widget.cpp
 * @brief Implementation of Host-side frequency Golden Sample measurement and Reference Mode panel.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "golden_reference_widget.h"
#include "core/app_config.h"
#include "database/db_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QAbstractSpinBox>
#include <QMessageBox>
#include <numeric>

GoldenReferenceWidget::GoldenReferenceWidget(DrxWebClient *refWeb, SerialWorker *refSerial, QWidget *parent)
    : QWidget(parent), m_refWebClient(refWeb), m_refSerial(refSerial) {

    setupUi();

    m_stepTimer.setSingleShot(true);
    connect(&m_stepTimer, &QTimer::timeout, this, &GoldenReferenceWidget::onCaptureStepTimeout);

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
}

void GoldenReferenceWidget::setupUi() {
    QVBoxLayout *rootVBox = new QVBoxLayout(this);
    rootVBox->setContentsMargins(10, 10, 10, 10);
    rootVBox->setSpacing(10);

    // ── 1. Referans Karşılaştırma Modu (Yönetici Yetkisi) ──────────────────────
    QGroupBox *grpMode = new QGroupBox("🎯 Yönetici Referans Modu (Tüm İstasyonları Belirler)", this);
    QHBoxLayout *modeHBox = new QHBoxLayout(grpMode);
    modeHBox->setSpacing(10);

    QLabel *lblInfo = new QLabel(
        "İstasyonlar kendi uydu profillerini test ederken hangi referansın kullanılacağını Yönetici seçer. "
        "Referans anten yoksa aşağıdan ölçüp kaydettiğiniz frekans verileriyle test yapılabilir:",
        grpMode);
    lblInfo->setWordWrap(true);
    lblInfo->setStyleSheet("color: #A7C4B5;");

    m_comboRefMode = new QComboBox(grpMode);
    m_comboRefMode->addItem("🟢 Canlı Referans Anten (Live Reference)", static_cast<int>(ReferenceMode::LiveReference));
    m_comboRefMode->addItem("🌟 Kayıtlı Golden Sample (Referans Anten Yokken)", static_cast<int>(ReferenceMode::GoldenSample));
    m_comboRefMode->setMinimumWidth(310);

    QPushButton *btnApply = new QPushButton("✅ Modu Uygula & İstasyonlara Bildir", grpMode);
    btnApply->setCursor(Qt::PointingHandCursor);
    btnApply->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnApply, &QPushButton::clicked, this, &GoldenReferenceWidget::onApplyRefModeClicked);

    modeHBox->addWidget(lblInfo, 1);
    modeHBox->addWidget(m_comboRefMode);
    modeHBox->addWidget(btnApply);
    rootVBox->addWidget(grpMode);

    // ── 2. Frekans Bazlı Referans Ölçüm Tablosu (Profil Bağımsız) ──────────────
    QGroupBox *grpFreq = new QGroupBox("🌟 Yönetici Frekans Referans (Golden Sample) Havuzu", this);
    QVBoxLayout *freqVBox = new QVBoxLayout(grpFreq);
    freqVBox->setSpacing(8);

    m_tableFreqs = new QTableWidget(grpFreq);
    m_tableFreqs->setColumnCount(9);
    m_tableFreqs->setHorizontalHeaderLabels({
        "#", "Etiket / Açıklama", "Frekans (MHz)", "Sembol Hızı", "Pol (V/H)",
        "Tolerans (dB)", "Golden C/N", "Golden POW", "Son Güncelleme"
    });
    m_tableFreqs->verticalHeader()->setVisible(false);
    m_tableFreqs->setAlternatingRowColors(true);
    m_tableFreqs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableFreqs->setColumnWidth(0, 36);
    m_tableFreqs->setColumnWidth(2, 115);
    m_tableFreqs->setColumnWidth(3, 110);
    m_tableFreqs->setColumnWidth(4, 68);
    m_tableFreqs->setColumnWidth(5, 96);
    m_tableFreqs->setColumnWidth(6, 94);
    m_tableFreqs->setColumnWidth(7, 100);
    m_tableFreqs->setColumnWidth(8, 145);
    freqVBox->addWidget(m_tableFreqs, 1);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnAdd = new QPushButton("➕ Frekans Ekle", grpFreq);
    btnAdd->setCursor(Qt::PointingHandCursor);
    connect(btnAdd, &QPushButton::clicked, this, &GoldenReferenceWidget::onAddRowClicked);

    QPushButton *btnDel = new QPushButton("➖ Seçili / Son Satırı Sil", grpFreq);
    btnDel->setCursor(Qt::PointingHandCursor);
    connect(btnDel, &QPushButton::clicked, this, &GoldenReferenceWidget::onRemoveRowClicked);

    m_btnCapture = new QPushButton("📸 Referans Antenden Ölç & Kaydet", grpFreq);
    m_btnCapture->setCursor(Qt::PointingHandCursor);
    m_btnCapture->setStyleSheet("background-color: #D97706; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(m_btnCapture, &QPushButton::clicked, this, &GoldenReferenceWidget::onStartCaptureClicked);

    QPushButton *btnSaveAll = new QPushButton("💾 Ölçümleri Kaydet", grpFreq);
    btnSaveAll->setCursor(Qt::PointingHandCursor);
    btnSaveAll->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnSaveAll, &QPushButton::clicked, this, &GoldenReferenceWidget::onSaveAllClicked);

    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnDel);
    btnRow->addStretch();
    btnRow->addWidget(m_btnCapture);
    btnRow->addWidget(btnSaveAll);
    freqVBox->addLayout(btnRow);

    m_lblStatus = new QLabel(
        "İpucu: Yönetici burada kendi ölçtüğü frekansları (Frekans, Sembol Hızı, V/H) kaydeder. "
        "Golden Sample modu açıldığında istasyonlar test ettikleri frekans için bu değerlerle kıyaslanır.",
        grpFreq);
    m_lblStatus->setWordWrap(true);
    m_lblStatus->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");
    freqVBox->addWidget(m_lblStatus);

    rootVBox->addWidget(grpFreq, 1);
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

void GoldenReferenceWidget::populateRow(int row, const GoldenFrequencyRecord &rec) {
    m_tableFreqs->setRowHeight(row, 36);

    auto *itNo = new QTableWidgetItem(QString::number(row + 1));
    itNo->setTextAlignment(Qt::AlignCenter);
    itNo->setFlags(itNo->flags() & ~Qt::ItemIsEditable);
    itNo->setData(Qt::UserRole, rec.id);
    m_tableFreqs->setItem(row, 0, itNo);

    m_tableFreqs->setItem(row, 1, new QTableWidgetItem(rec.label));

    auto *spinFreq = new QSpinBox(m_tableFreqs);
    spinFreq->setRange(950, 14500);
    spinFreq->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinFreq->setAlignment(Qt::AlignCenter);
    spinFreq->setSuffix(" MHz");
    spinFreq->setValue(qBound(950, rec.frequencyMhz, 14500));
    m_tableFreqs->setCellWidget(row, 2, spinFreq);

    auto *spinSr = new QSpinBox(m_tableFreqs);
    spinSr->setRange(1000, 65000);
    spinSr->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinSr->setAlignment(Qt::AlignCenter);
    spinSr->setSuffix(" kS/s");
    spinSr->setValue(qBound(1000, rec.symbolRate, 65000));
    m_tableFreqs->setCellWidget(row, 3, spinSr);

    auto *comboPol = new QComboBox(m_tableFreqs);
    comboPol->addItem("V");
    comboPol->addItem("H");
    comboPol->setCurrentIndex(rec.polarization.trimmed().toUpper() == "H" ? 1 : 0);
    m_tableFreqs->setCellWidget(row, 4, comboPol);

    auto *spinTol = new QDoubleSpinBox(m_tableFreqs);
    spinTol->setRange(0.10, 10.00);
    spinTol->setDecimals(2);
    spinTol->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinTol->setAlignment(Qt::AlignCenter);
    spinTol->setSuffix(" dB");
    spinTol->setValue(qBound(0.10, rec.toleranceDb, 10.00));
    m_tableFreqs->setCellWidget(row, 5, spinTol);

    auto *spinCn = new QDoubleSpinBox(m_tableFreqs);
    spinCn->setRange(0.0, 40.0);
    spinCn->setDecimals(2);
    spinCn->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinCn->setAlignment(Qt::AlignCenter);
    spinCn->setSuffix(" dB");
    spinCn->setValue(qBound(0.0, rec.goldenCnDb, 40.0));
    m_tableFreqs->setCellWidget(row, 6, spinCn);

    auto *spinPow = new QDoubleSpinBox(m_tableFreqs);
    spinPow->setRange(-120.0, 20.0);
    spinPow->setDecimals(2);
    spinPow->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinPow->setAlignment(Qt::AlignCenter);
    spinPow->setSuffix(" dBm");
    spinPow->setValue(qBound(-120.0, rec.goldenPowDbm, 20.0));
    m_tableFreqs->setCellWidget(row, 7, spinPow);

    QString stStr = rec.updatedAt.isEmpty() ? ((rec.goldenCnDb > 0.1) ? "✔ KAYITLI" : "⚪ ÖLÇÜLMEDİ") : rec.updatedAt;
    auto *itStat = new QTableWidgetItem(stStr);
    itStat->setTextAlignment(Qt::AlignCenter);
    itStat->setFlags(itStat->flags() & ~Qt::ItemIsEditable);
    itStat->setForeground(QBrush(QColor((rec.goldenCnDb > 0.1 || !rec.updatedAt.isEmpty()) ? "#34D399" : "#94A3B8")));
    m_tableFreqs->setItem(row, 8, itStat);
}

GoldenFrequencyRecord GoldenReferenceWidget::readRow(int row) const {
    GoldenFrequencyRecord rec;
    if (!m_tableFreqs || row < 0 || row >= m_tableFreqs->rowCount()) return rec;

    if (auto *itNo = m_tableFreqs->item(row, 0)) {
        rec.id = itNo->data(Qt::UserRole).toInt();
    }
    if (auto *itLbl = m_tableFreqs->item(row, 1)) {
        rec.label = itLbl->text().trimmed();
    }
    if (auto *w = qobject_cast<QSpinBox*>(m_tableFreqs->cellWidget(row, 2))) rec.frequencyMhz = w->value();
    if (auto *w = qobject_cast<QSpinBox*>(m_tableFreqs->cellWidget(row, 3))) rec.symbolRate = w->value();
    if (auto *w = qobject_cast<QComboBox*>(m_tableFreqs->cellWidget(row, 4))) rec.polarization = w->currentText();
    if (auto *w = qobject_cast<QDoubleSpinBox*>(m_tableFreqs->cellWidget(row, 5))) rec.toleranceDb = w->value();
    if (auto *w = qobject_cast<QDoubleSpinBox*>(m_tableFreqs->cellWidget(row, 6))) rec.goldenCnDb = w->value();
    if (auto *w = qobject_cast<QDoubleSpinBox*>(m_tableFreqs->cellWidget(row, 7))) rec.goldenPowDbm = w->value();
    return rec;
}

void GoldenReferenceWidget::reloadFrequencies() {
    if (!m_tableFreqs) return;
    m_records = DbManager::instance().getGoldenFrequencies();
    m_tableFreqs->setRowCount(m_records.size());
    for (int i = 0; i < m_records.size(); ++i) {
        populateRow(i, m_records[i]);
    }
}

void GoldenReferenceWidget::onAddRowClicked() {
    if (!m_tableFreqs) return;
    int row = m_tableFreqs->rowCount();
    m_tableFreqs->insertRow(row);

    GoldenFrequencyRecord rec;
    rec.label = QString("Frekans %1").arg(row + 1);
    rec.frequencyMhz = 11054;
    rec.symbolRate = 30000;
    rec.polarization = "V";
    rec.toleranceDb = AppConfig::instance().defaultToleranceDb();
    populateRow(row, rec);
    m_tableFreqs->scrollToBottom();
}

void GoldenReferenceWidget::onRemoveRowClicked() {
    if (!m_tableFreqs || m_tableFreqs->rowCount() == 0) return;
    int selRow = m_tableFreqs->currentRow();
    int removeIdx = (selRow >= 0 && selRow < m_tableFreqs->rowCount()) ? selRow : (m_tableFreqs->rowCount() - 1);

    int id = m_tableFreqs->item(removeIdx, 0) ? m_tableFreqs->item(removeIdx, 0)->data(Qt::UserRole).toInt() : 0;
    if (id > 0) {
        DbManager::instance().deleteGoldenFrequency(id);
    }
    m_tableFreqs->removeRow(removeIdx);
    for (int r = 0; r < m_tableFreqs->rowCount(); ++r) {
        if (auto *it = m_tableFreqs->item(r, 0)) it->setText(QString::number(r + 1));
    }
}

void GoldenReferenceWidget::onSaveAllClicked() {
    if (!m_tableFreqs) return;
    int savedCount = 0;
    for (int r = 0; r < m_tableFreqs->rowCount(); ++r) {
        GoldenFrequencyRecord rec = readRow(r);
        if (DbManager::instance().saveGoldenFrequency(rec)) {
            savedCount++;
        }
    }
    reloadFrequencies();
    emit logMessage(QString("[Golden Havuz] %1 frekans ölçümü veritabanına kaydedildi.").arg(savedCount));
    QMessageBox::information(this, "Kaydedildi", QString("%1 frekans referans kaydı güncellendi.").arg(savedCount));
}

void GoldenReferenceWidget::onReferenceTelemetryReceived(double powDbm, double cnDb) {
    if (m_captureActive) {
        m_cnSamples.append(cnDb);
        m_powSamples.append(powDbm);
    }
}

void GoldenReferenceWidget::onStartCaptureClicked() {
    if (m_captureActive) {
        m_captureActive = false;
        m_stepTimer.stop();
        m_btnCapture->setText("📸 Referans Antenden Ölç & Kaydet");
        m_lblStatus->setText("Ölçüm durduruldu.");
        return;
    }

    bool refReady = (m_refWebClient && m_refWebClient->isReachable()) ||
                    (m_refSerial && m_refSerial->isConnected());
    if (!refReady) {
        QMessageBox::warning(this, "Referans DRX Bağlı Değil",
                             "Otomatik ölçüm almak için Referans DRX9000 cihazının Ethernet (Ping OK) veya RS232 ile bağlı olması gerekir.\n\n"
                             "İpucu: Golden C/N ve POW sütunlarını elle girip 'Ölçümleri Kaydet' ile de saklayabilirsiniz.");
        return;
    }

    if (m_tableFreqs->rowCount() == 0) {
        QMessageBox::information(this, "Frekans Yok", "Lütfen önce '+ Frekans Ekle' ile ölçülecek frekans ekleyin.");
        return;
    }

    m_captureActive = true;
    m_captureRow = 0;
    m_btnCapture->setText("⏹ Ölçümü Durdur");
    triggerCaptureForCurrentRow();
}

void GoldenReferenceWidget::triggerCaptureForCurrentRow() {
    if (!m_captureActive || !m_tableFreqs) return;
    GoldenFrequencyRecord rec = readRow(m_captureRow);
    m_cnSamples.clear();
    m_powSamples.clear();
    m_lblStatus->setText(QString("⏳ Ölçülüyor [%1/%2]: %3 MHz (%4)...")
                             .arg(m_captureRow + 1).arg(m_tableFreqs->rowCount())
                             .arg(rec.frequencyMhz).arg(rec.polarization));

    if (m_refWebClient && m_refWebClient->isReachable()) {
        m_refWebClient->sendTune(rec.frequencyMhz, rec.symbolRate, rec.polarization);
        QTimer::singleShot(900, this, [this]() { if (m_captureActive && m_refWebClient) m_refWebClient->requestStatus(); });
        QTimer::singleShot(1600, this, [this]() { if (m_captureActive && m_refWebClient) m_refWebClient->requestStatus(); });
    }
    if (m_refSerial && m_refSerial->isConnected()) {
        m_refSerial->sendTune(rec.frequencyMhz, rec.symbolRate, rec.polarization);
        QTimer::singleShot(1000, this, [this]() { if (m_captureActive && m_refSerial) m_refSerial->requestStatus(); });
    }
    m_stepTimer.start(2400);
}

void GoldenReferenceWidget::onCaptureStepTimeout() {
    if (!m_captureActive || !m_tableFreqs) return;

    GoldenFrequencyRecord rec = readRow(m_captureRow);
    if (!m_cnSamples.isEmpty()) {
        rec.goldenCnDb = std::accumulate(m_cnSamples.begin(), m_cnSamples.end(), 0.0) / m_cnSamples.size();
        rec.goldenPowDbm = std::accumulate(m_powSamples.begin(), m_powSamples.end(), 0.0) / m_powSamples.size();

        if (auto *w = qobject_cast<QDoubleSpinBox*>(m_tableFreqs->cellWidget(m_captureRow, 6))) w->setValue(rec.goldenCnDb);
        if (auto *w = qobject_cast<QDoubleSpinBox*>(m_tableFreqs->cellWidget(m_captureRow, 7))) w->setValue(rec.goldenPowDbm);
        if (auto *it = m_tableFreqs->item(m_captureRow, 8)) {
            it->setText("✔ ÖLÇÜLDÜ");
            it->setForeground(QBrush(QColor("#34D399")));
        }

        DbManager::instance().saveGoldenFrequency(rec);
        emit logMessage(QString("[Golden Havuz] %1 MHz (%2) kaydedildi: C/N=%3 dB | POW=%4 dBm")
                            .arg(rec.frequencyMhz).arg(rec.polarization)
                            .arg(rec.goldenCnDb, 0, 'f', 2).arg(rec.goldenPowDbm, 0, 'f', 2));
    } else {
        if (auto *it = m_tableFreqs->item(m_captureRow, 8)) {
            it->setText("⚠ VERİ YOK");
            it->setForeground(QBrush(QColor("#FBBF24")));
        }
    }

    m_captureRow++;
    if (m_captureRow < m_tableFreqs->rowCount()) {
        triggerCaptureForCurrentRow();
    } else {
        m_captureActive = false;
        m_btnCapture->setText("📸 Referans Antenden Ölç & Kaydet");
        m_lblStatus->setText("✔ Tüm frekans ölçümleri tamamlandı ve veritabanına kaydedildi!");
        reloadFrequencies();
        QMessageBox::information(this, "Ölçüm Tamamlandı", "Tüm frekanslar için Golden Sample referansları kaydedildi.");
    }
}
