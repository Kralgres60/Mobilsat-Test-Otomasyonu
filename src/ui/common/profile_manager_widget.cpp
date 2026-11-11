/**
 * @file profile_manager_widget.cpp
 * @brief Implementation of the reusable satellite profile & transponder frequency editor widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "profile_manager_widget.h"
#include "core/app_config.h"
#include "database/db_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <numeric>

ProfileManagerWidget::ProfileManagerWidget(bool isHostMode,
                                           DrxWebClient *refWeb,
                                           SerialWorker *refSerial,
                                           QWidget *parent)
    : QWidget(parent),
      m_isHostMode(isHostMode),
      m_refWebClient(refWeb),
      m_refSerial(refSerial) {

    setupUi();

    m_goldenStepTimer.setSingleShot(true);
    connect(&m_goldenStepTimer, &QTimer::timeout, this, &ProfileManagerWidget::onGoldenCaptureStepTimeout);

    if (m_isHostMode) {
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
}

void ProfileManagerWidget::setupUi() {
    QVBoxLayout *rootVBox = new QVBoxLayout(this);
    rootVBox->setContentsMargins(10, 10, 10, 10);
    rootVBox->setSpacing(10);

    // ── 1. Host Reference Mode Selector (Administrator Only) ─────────────────
    if (m_isHostMode) {
        QGroupBox *grpRefMode = new QGroupBox("🎯 Yönetici Referans Karşılaştırma Modu (Tüm İstasyonları Bağlar)", this);
        QHBoxLayout *modeHBox = new QHBoxLayout(grpRefMode);
        modeHBox->setSpacing(10);

        QLabel *lblModeInfo = new QLabel(
            "Test istasyonları yönetici onaylı referans kaynağını kullanır. Referans anten yokken kayıtlı Golden Sample moduna geçebilirsiniz:",
            grpRefMode);
        lblModeInfo->setWordWrap(true);
        lblModeInfo->setStyleSheet("color: #A7C4B5;");

        m_comboRefMode = new QComboBox(grpRefMode);
        m_comboRefMode->addItem("🟢 Canlı Referans Anten (Live Reference)", static_cast<int>(ReferenceMode::LiveReference));
        m_comboRefMode->addItem("🌟 Kayıtlı Golden Sample (Referans Anten Yokken)", static_cast<int>(ReferenceMode::GoldenSample));
        m_comboRefMode->setMinimumWidth(300);

        QPushButton *btnApplyMode = new QPushButton("✅ Modu Uygula & İstasyonlara Bildir", grpRefMode);
        btnApplyMode->setCursor(Qt::PointingHandCursor);
        btnApplyMode->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
        connect(btnApplyMode, &QPushButton::clicked, this, &ProfileManagerWidget::onApplyRefModeClicked);

        modeHBox->addWidget(lblModeInfo, 1);
        modeHBox->addWidget(m_comboRefMode);
        modeHBox->addWidget(btnApplyMode);
        rootVBox->addWidget(grpRefMode);
    }

    // ── 2. Split Profile List (Left) & Transponder Frequencies Table (Right) ─
    QHBoxLayout *splitHBox = new QHBoxLayout();
    splitHBox->setSpacing(10);

    QGroupBox *grpProfLeft = new QGroupBox("🛰️ Uydu & Frekans Profilleri", this);
    grpProfLeft->setFixedWidth(340);
    QVBoxLayout *leftVBox = new QVBoxLayout(grpProfLeft);
    leftVBox->setSpacing(8);

    m_listProfiles = new QListWidget(grpProfLeft);
    connect(m_listProfiles, &QListWidget::currentRowChanged, this, &ProfileManagerWidget::onProfileSelected);
    leftVBox->addWidget(m_listProfiles, 1);

    QHBoxLayout *profBtnRow = new QHBoxLayout();
    QPushButton *btnNewProf = new QPushButton("➕ Yeni Profil", grpProfLeft);
    btnNewProf->setCursor(Qt::PointingHandCursor);
    btnNewProf->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 10px; font-weight: bold;");
    connect(btnNewProf, &QPushButton::clicked, this, &ProfileManagerWidget::onNewProfileClicked);

    QPushButton *btnDelProf = new QPushButton("🗑️ Sil", grpProfLeft);
    btnDelProf->setCursor(Qt::PointingHandCursor);
    btnDelProf->setStyleSheet("background-color: #991B1B; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 10px; font-weight: bold;");
    connect(btnDelProf, &QPushButton::clicked, this, &ProfileManagerWidget::onDeleteProfileClicked);

    profBtnRow->addWidget(btnNewProf, 2);
    profBtnRow->addWidget(btnDelProf, 1);
    leftVBox->addLayout(profBtnRow);

    QFormLayout *profForm = new QFormLayout();
    profForm->setSpacing(6);
    m_txtProfName = new QLineEdit(grpProfLeft);
    m_txtProfName->setPlaceholderText("Örn: Türksat 42E Hızlı Test");
    m_txtSatName = new QLineEdit(grpProfLeft);
    m_txtSatName->setPlaceholderText("Örn: Türksat 42.0°E");
    m_txtOrbitalPos = new QLineEdit(grpProfLeft);
    m_txtOrbitalPos->setPlaceholderText("Örn: 42.0E");
    m_spinProfTol = new QDoubleSpinBox(grpProfLeft);
    m_spinProfTol->setRange(0.20, 10.00);
    m_spinProfTol->setSingleStep(0.10);
    m_spinProfTol->setSuffix(" dB");
    m_spinProfTol->setValue(1.50);

    profForm->addRow("Profil Adı:", m_txtProfName);
    profForm->addRow("Uydu Adı:", m_txtSatName);
    profForm->addRow("Yörünge:", m_txtOrbitalPos);
    profForm->addRow("Vars. Tolerans:", m_spinProfTol);
    leftVBox->addLayout(profForm);

    splitHBox->addWidget(grpProfLeft);

    // Sağ Kolon: Frekans & Golden Sample Düzenleme Tablosu
    QGroupBox *grpChannels = new QGroupBox("📺 Profil Frekansları & Transponder Listesi", this);
    QVBoxLayout *chVBox = new QVBoxLayout(grpChannels);
    chVBox->setSpacing(8);

    m_tableProfileChannels = new QTableWidget(grpChannels);
    m_tableProfileChannels->setColumnCount(9);
    m_tableProfileChannels->setHorizontalHeaderLabels({
        "#", "Kanal Adı", "Frekans (MHz)", "Sembol Hızı", "Pol (V/H)", "Tolerans (dB)", "Golden C/N", "Golden POW", "Golden Durum"
    });
    m_tableProfileChannels->verticalHeader()->setVisible(false);
    m_tableProfileChannels->setAlternatingRowColors(true);
    m_tableProfileChannels->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableProfileChannels->setColumnWidth(0, 36);
    m_tableProfileChannels->setColumnWidth(2, 95);
    m_tableProfileChannels->setColumnWidth(3, 90);
    m_tableProfileChannels->setColumnWidth(4, 66);
    m_tableProfileChannels->setColumnWidth(5, 92);
    m_tableProfileChannels->setColumnWidth(6, 86);
    m_tableProfileChannels->setColumnWidth(7, 88);
    m_tableProfileChannels->setColumnWidth(8, 105);
    chVBox->addWidget(m_tableProfileChannels, 1);

    QHBoxLayout *chActionRow = new QHBoxLayout();
    QPushButton *btnAddCh = new QPushButton("➕ Frekans / Kanal Ekle", grpChannels);
    btnAddCh->setCursor(Qt::PointingHandCursor);
    connect(btnAddCh, &QPushButton::clicked, this, &ProfileManagerWidget::onAddChannelRowClicked);

    QPushButton *btnRemCh = new QPushButton("➖ Seçili / Son Kanalı Sil", grpChannels);
    btnRemCh->setCursor(Qt::PointingHandCursor);
    connect(btnRemCh, &QPushButton::clicked, this, &ProfileManagerWidget::onRemoveChannelRowClicked);

    QPushButton *btnSaveProf = new QPushButton("💾 Profili & Frekansları Kaydet", grpChannels);
    btnSaveProf->setCursor(Qt::PointingHandCursor);
    btnSaveProf->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnSaveProf, &QPushButton::clicked, this, &ProfileManagerWidget::onSaveProfileClicked);

    chActionRow->addWidget(btnAddCh);
    chActionRow->addWidget(btnRemCh);
    chActionRow->addStretch();

    if (m_isHostMode) {
        m_btnGoldenCapture = new QPushButton("📸 Referanstan Golden Sample Ölç", grpChannels);
        m_btnGoldenCapture->setCursor(Qt::PointingHandCursor);
        m_btnGoldenCapture->setStyleSheet("background-color: #D97706; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
        connect(m_btnGoldenCapture, &QPushButton::clicked, this, &ProfileManagerWidget::onStartGoldenCaptureClicked);
        chActionRow->addWidget(m_btnGoldenCapture);
    }

    chActionRow->addWidget(btnSaveProf);
    chVBox->addLayout(chActionRow);

    m_lblGoldenCaptureStatus = new QLabel(
        m_isHostMode
            ? "İpucu: Profil ve frekansları düzenleyip kaydedebilir, Golden C/N ve POW değerlerini elle girebilir veya Referans DRX'ten otomatik ölçüm alabilirsiniz."
            : "İpucu: İstasyon için yeni uydu profili oluşturabilir, frekans (MHz), sembol hızı (kS/s) ve polarizasyon (V/H) ekleyip merkezi veritabanına kaydedebilirsiniz.",
        grpChannels);
    m_lblGoldenCaptureStatus->setWordWrap(true);
    m_lblGoldenCaptureStatus->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");
    chVBox->addWidget(m_lblGoldenCaptureStatus);

    splitHBox->addWidget(grpChannels, 1);
    rootVBox->addLayout(splitHBox, 1);
}

void ProfileManagerWidget::setActiveReferenceMode(ReferenceMode mode) {
    if (m_comboRefMode) {
        m_comboRefMode->setCurrentIndex(mode == ReferenceMode::GoldenSample ? 1 : 0);
    }
}

void ProfileManagerWidget::onApplyRefModeClicked() {
    if (!m_comboRefMode) return;
    ReferenceMode mode = static_cast<ReferenceMode>(m_comboRefMode->currentData().toInt());
    emit referenceModeApplyRequested(mode);
}

void ProfileManagerWidget::reloadProfiles(int selectProfileId) {
    if (!m_listProfiles) return;
    m_profiles = DbManager::instance().getProfiles();

    m_listProfiles->blockSignals(true);
    m_listProfiles->clear();
    int targetRow = 0;
    for (int i = 0; i < m_profiles.size(); ++i) {
        const auto &p = m_profiles[i];
        auto *item = new QListWidgetItem(QString("%1 (%2) [%3 Kanal]")
                                             .arg(p.profileName, p.orbitalPosition)
                                             .arg(p.channels.size()));
        item->setData(Qt::UserRole, p.id);
        m_listProfiles->addItem(item);
        if (p.id == selectProfileId) {
            targetRow = i;
        }
    }
    m_listProfiles->blockSignals(false);

    if (!m_profiles.isEmpty()) {
        m_listProfiles->setCurrentRow(targetRow);
        onProfileSelected(targetRow);
    }
}

void ProfileManagerWidget::onProfileSelected(int row) {
    if (row < 0 || row >= m_profiles.size() || !m_tableProfileChannels) return;
    const auto &p = m_profiles[row];

    m_txtProfName->setText(p.profileName);
    m_txtSatName->setText(p.satelliteName);
    m_txtOrbitalPos->setText(p.orbitalPosition);
    m_spinProfTol->setValue(p.defaultToleranceDb);

    m_tableProfileChannels->setRowCount(p.channels.size());
    for (int i = 0; i < p.channels.size(); ++i) {
        const auto &ch = p.channels[i];
        m_tableProfileChannels->setRowHeight(i, 30);

        auto *itNo = new QTableWidgetItem(QString::number(ch.channelNo));
        itNo->setTextAlignment(Qt::AlignCenter);
        itNo->setFlags(itNo->flags() & ~Qt::ItemIsEditable);

        m_tableProfileChannels->setItem(i, 0, itNo);
        m_tableProfileChannels->setItem(i, 1, new QTableWidgetItem(ch.channelName));
        m_tableProfileChannels->setItem(i, 2, new QTableWidgetItem(QString::number(ch.frequencyMhz)));
        m_tableProfileChannels->setItem(i, 3, new QTableWidgetItem(QString::number(ch.symbolRate)));
        m_tableProfileChannels->setItem(i, 4, new QTableWidgetItem(ch.polarization));
        m_tableProfileChannels->setItem(i, 5, new QTableWidgetItem(QString::number(ch.toleranceDb, 'f', 2)));
        m_tableProfileChannels->setItem(i, 6, new QTableWidgetItem(QString::number(ch.goldenCnDb, 'f', 2)));
        m_tableProfileChannels->setItem(i, 7, new QTableWidgetItem(QString::number(ch.goldenPowDbm, 'f', 2)));

        auto *itStat = new QTableWidgetItem(ch.hasGoldenSample ? "✔ KAYITLI" : "⚪ YOK");
        itStat->setTextAlignment(Qt::AlignCenter);
        itStat->setFlags(itStat->flags() & ~Qt::ItemIsEditable);
        itStat->setForeground(QBrush(QColor(ch.hasGoldenSample ? "#34D399" : "#94A3B8")));
        m_tableProfileChannels->setItem(i, 8, itStat);
    }
}

void ProfileManagerWidget::onNewProfileClicked() {
    SatelliteProfileInfo p;
    p.id = 0;
    p.profileName = QString("Yeni Uydu Profili %1").arg(m_profiles.size() + 1);
    p.satelliteName = "Türksat 42.0°E";
    p.orbitalPosition = "42.0E";
    p.defaultToleranceDb = AppConfig::instance().defaultToleranceDb();
    p.isActive = true;

    ChannelConfig ch;
    ch.channelNo = 1;
    ch.channelName = "TRT 1 HD";
    ch.frequencyMhz = 11054;
    ch.symbolRate = 30000;
    ch.polarization = "V";
    ch.toleranceDb = p.defaultToleranceDb;
    p.channels.append(ch);

    if (DbManager::instance().saveProfile(p)) {
        reloadProfiles();
        m_listProfiles->setCurrentRow(m_listProfiles->count() - 1);
        emit logMessage("[Profiller] Yeni uydu profili oluşturuldu: " + p.profileName);
        emit profilesChanged();
    } else {
        QMessageBox::warning(this, "Hata", "Yeni profil oluşturulamadı:\n" + DbManager::instance().lastError());
    }
}

void ProfileManagerWidget::onDeleteProfileClicked() {
    int row = m_listProfiles ? m_listProfiles->currentRow() : -1;
    if (row < 0 || row >= m_profiles.size()) return;
    int pid = m_profiles[row].id;
    if (pid <= 1) {
        QMessageBox::information(this, "Korunan Profil", "Varsayılan fabrika profili (ID 1) silinemez.");
        return;
    }
    if (DbManager::instance().deleteProfile(pid)) {
        emit logMessage("[Profiller] Uydu profili silindi (ID " + QString::number(pid) + ").");
        reloadProfiles();
        emit profilesChanged();
    }
}

void ProfileManagerWidget::onAddChannelRowClicked() {
    if (!m_tableProfileChannels) return;
    int row = m_tableProfileChannels->rowCount();
    m_tableProfileChannels->insertRow(row);
    m_tableProfileChannels->setRowHeight(row, 30);

    auto *itNo = new QTableWidgetItem(QString::number(row + 1));
    itNo->setTextAlignment(Qt::AlignCenter);
    itNo->setFlags(itNo->flags() & ~Qt::ItemIsEditable);

    m_tableProfileChannels->setItem(row, 0, itNo);
    m_tableProfileChannels->setItem(row, 1, new QTableWidgetItem(QString("Frekans %1").arg(row + 1)));
    m_tableProfileChannels->setItem(row, 2, new QTableWidgetItem("11054"));
    m_tableProfileChannels->setItem(row, 3, new QTableWidgetItem("30000"));
    m_tableProfileChannels->setItem(row, 4, new QTableWidgetItem("V"));
    m_tableProfileChannels->setItem(row, 5, new QTableWidgetItem(QString::number(m_spinProfTol->value(), 'f', 2)));
    m_tableProfileChannels->setItem(row, 6, new QTableWidgetItem("0.00"));
    m_tableProfileChannels->setItem(row, 7, new QTableWidgetItem("0.00"));

    auto *itStat = new QTableWidgetItem("⚪ YOK");
    itStat->setTextAlignment(Qt::AlignCenter);
    itStat->setFlags(itStat->flags() & ~Qt::ItemIsEditable);
    itStat->setForeground(QBrush(QColor("#94A3B8")));
    m_tableProfileChannels->setItem(row, 8, itStat);
    m_tableProfileChannels->scrollToBottom();
}

void ProfileManagerWidget::onRemoveChannelRowClicked() {
    if (!m_tableProfileChannels) return;
    int rows = m_tableProfileChannels->rowCount();
    if (rows <= 1) return;

    int selRow = m_tableProfileChannels->currentRow();
    int removeIdx = (selRow >= 0 && selRow < rows) ? selRow : (rows - 1);
    m_tableProfileChannels->removeRow(removeIdx);

    // Re-number channel sequence indices cleanly
    for (int r = 0; r < m_tableProfileChannels->rowCount(); ++r) {
        if (auto *it = m_tableProfileChannels->item(r, 0)) {
            it->setText(QString::number(r + 1));
        }
    }
}

void ProfileManagerWidget::onSaveProfileClicked() {
    int row = m_listProfiles ? m_listProfiles->currentRow() : -1;
    if (row < 0 || row >= m_profiles.size()) return;

    SatelliteProfileInfo p = m_profiles[row];
    p.profileName = m_txtProfName->text().trimmed();
    p.satelliteName = m_txtSatName->text().trimmed();
    p.orbitalPosition = m_txtOrbitalPos->text().trimmed();
    p.defaultToleranceDb = m_spinProfTol->value();
    p.isActive = true;
    p.channels.clear();

    if (p.profileName.isEmpty()) {
        QMessageBox::warning(this, "Eksik Bilgi", "Profil adı boş olamaz.");
        return;
    }

    for (int r = 0; r < m_tableProfileChannels->rowCount(); ++r) {
        ChannelConfig ch;
        ch.channelNo = r + 1;
        ch.channelName = m_tableProfileChannels->item(r, 1) ? m_tableProfileChannels->item(r, 1)->text().trimmed() : QString("Frekans %1").arg(r + 1);
        ch.frequencyMhz = m_tableProfileChannels->item(r, 2) ? m_tableProfileChannels->item(r, 2)->text().toInt() : 11054;
        ch.symbolRate = m_tableProfileChannels->item(r, 3) ? m_tableProfileChannels->item(r, 3)->text().toInt() : 30000;
        QString pol = m_tableProfileChannels->item(r, 4) ? m_tableProfileChannels->item(r, 4)->text().trimmed().toUpper() : "V";
        ch.polarization = (pol == "H") ? "H" : "V";
        ch.toleranceDb = m_tableProfileChannels->item(r, 5) ? m_tableProfileChannels->item(r, 5)->text().toDouble() : p.defaultToleranceDb;
        ch.goldenCnDb = m_tableProfileChannels->item(r, 6) ? m_tableProfileChannels->item(r, 6)->text().toDouble() : 0.0;
        ch.goldenPowDbm = m_tableProfileChannels->item(r, 7) ? m_tableProfileChannels->item(r, 7)->text().toDouble() : 0.0;
        ch.hasGoldenSample = (ch.goldenCnDb > 0.1 || qAbs(ch.goldenPowDbm) > 0.1);
        p.channels.append(ch);
    }

    if (DbManager::instance().saveProfile(p)) {
        emit logMessage("[Profiller] Uydu profili ve frekanslar kaydedildi: " + p.profileName);
        reloadProfiles(p.id);
        emit profilesChanged();
        QMessageBox::information(this, "Kaydedildi",
                                 QString("'%1' uydu profili ve %2 adet frekans/kanal veritabanına kaydedildi.")
                                     .arg(p.profileName)
                                     .arg(p.channels.size()));
    } else {
        QMessageBox::warning(this, "Kayıt Hatası", "Profil kaydedilemedi:\n" + DbManager::instance().lastError());
    }
}

void ProfileManagerWidget::onReferenceTelemetryReceived(double powDbm, double cnDb) {
    if (m_goldenCaptureActive) {
        m_goldenCnSamples.append(cnDb);
        m_goldenPowSamples.append(powDbm);
    }
}

void ProfileManagerWidget::onStartGoldenCaptureClicked() {
    if (!m_isHostMode) return;

    if (m_goldenCaptureActive) {
        m_goldenCaptureActive = false;
        m_goldenStepTimer.stop();
        m_btnGoldenCapture->setText("📸 Referanstan Golden Sample Ölç");
        m_lblGoldenCaptureStatus->setText("Golden Sample ölçümü kullanıcı tarafından durduruldu.");
        return;
    }

    int row = m_listProfiles ? m_listProfiles->currentRow() : -1;
    if (row < 0 || row >= m_profiles.size()) return;

    bool refReady = (m_refWebClient && m_refWebClient->isReachable()) ||
                    (m_refSerial && m_refSerial->isConnected());
    if (!refReady) {
        QMessageBox::warning(this, "Referans DRX Bağlı Değil",
                             "Golden Sample ölçümü almak için Referans DRX9000 cihazının Ethernet (Ping OK) veya RS232 üzerinden bağlı olması gerekir.\n\n"
                             "İpucu: Tablodaki Golden C/N ve Golden POW sütunlarını elle de düzenleyip kaydedebilirsiniz.");
        return;
    }

    if (m_tableProfileChannels->rowCount() == 0) return;

    m_goldenCaptureActive = true;
    m_goldenCaptureProfileId = m_profiles[row].id;
    m_goldenCaptureRow = 0;
    m_btnGoldenCapture->setText("⏹ Ölçümü Durdur");
    triggerGoldenCaptureCurrentRow();
}

void ProfileManagerWidget::triggerGoldenCaptureCurrentRow() {
    if (!m_goldenCaptureActive || !m_tableProfileChannels) return;
    int freq = m_tableProfileChannels->item(m_goldenCaptureRow, 2)->text().toInt();
    int sr = m_tableProfileChannels->item(m_goldenCaptureRow, 3)->text().toInt();
    QString pol = m_tableProfileChannels->item(m_goldenCaptureRow, 4)->text().trimmed().toUpper();
    QString name = m_tableProfileChannels->item(m_goldenCaptureRow, 1)->text();

    m_goldenCnSamples.clear();
    m_goldenPowSamples.clear();
    m_lblGoldenCaptureStatus->setText(
        QString("⏳ Ölçülüyor [%1/%2]: %3 (%4 MHz %5)...")
            .arg(m_goldenCaptureRow + 1)
            .arg(m_tableProfileChannels->rowCount())
            .arg(name).arg(freq).arg(pol));

    if (m_refWebClient && m_refWebClient->isReachable()) {
        m_refWebClient->sendTune(freq, sr, pol);
        QTimer::singleShot(900, this, [this]() { if (m_goldenCaptureActive && m_refWebClient) m_refWebClient->requestStatus(); });
        QTimer::singleShot(1600, this, [this]() { if (m_goldenCaptureActive && m_refWebClient) m_refWebClient->requestStatus(); });
    }
    if (m_refSerial && m_refSerial->isConnected()) {
        m_refSerial->sendTune(freq, sr, pol);
        QTimer::singleShot(1000, this, [this]() { if (m_goldenCaptureActive && m_refSerial) m_refSerial->requestStatus(); });
    }
    m_goldenStepTimer.start(2400);
}

void ProfileManagerWidget::onGoldenCaptureStepTimeout() {
    if (!m_goldenCaptureActive || !m_tableProfileChannels) return;

    if (!m_goldenCnSamples.isEmpty()) {
        double avgCn = std::accumulate(m_goldenCnSamples.begin(), m_goldenCnSamples.end(), 0.0) / m_goldenCnSamples.size();
        double avgPow = std::accumulate(m_goldenPowSamples.begin(), m_goldenPowSamples.end(), 0.0) / m_goldenPowSamples.size();
        m_tableProfileChannels->item(m_goldenCaptureRow, 6)->setText(QString::number(avgCn, 'f', 2));
        m_tableProfileChannels->item(m_goldenCaptureRow, 7)->setText(QString::number(avgPow, 'f', 2));
        m_tableProfileChannels->item(m_goldenCaptureRow, 8)->setText("✔ ÖLÇÜLDÜ");
        m_tableProfileChannels->item(m_goldenCaptureRow, 8)->setForeground(QBrush(QColor("#34D399")));

        int chNo = m_goldenCaptureRow + 1;
        DbManager::instance().updateChannelGoldenSample(m_goldenCaptureProfileId, chNo, avgCn, avgPow);
        emit logMessage(QString("[Golden Sample] CH%1 kaydedildi: C/N=%2 dB | POW=%3 dBm").arg(chNo).arg(avgCn, 0, 'f', 2).arg(avgPow, 0, 'f', 2));
    } else {
        m_tableProfileChannels->item(m_goldenCaptureRow, 8)->setText("⚠ VERİ YOK");
        m_tableProfileChannels->item(m_goldenCaptureRow, 8)->setForeground(QBrush(QColor("#FBBF24")));
    }

    m_goldenCaptureRow++;
    if (m_goldenCaptureRow < m_tableProfileChannels->rowCount()) {
        triggerGoldenCaptureCurrentRow();
    } else {
        m_goldenCaptureActive = false;
        m_btnGoldenCapture->setText("📸 Referanstan Golden Sample Ölç");
        m_lblGoldenCaptureStatus->setText("✔ Tüm kanallar için Golden Sample ölçümü tamamlandı ve veritabanına kaydedildi!");
        reloadProfiles(m_goldenCaptureProfileId);
        emit profilesChanged();
        QMessageBox::information(this, "Golden Sample Tamamlandı",
                                 "Seçili uydu profilindeki tüm kanallar için Golden Sample referans ölçümleri tamamlandı ve kaydedildi.");
    }
}
