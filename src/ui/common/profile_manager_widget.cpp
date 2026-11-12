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
#include <QInputDialog>
#include <QSpinBox>
#include <QAbstractSpinBox>
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
    if (m_isHostMode) {
        profForm->addRow("Vars. Tolerans:", m_spinProfTol);
    } else {
        m_spinProfTol->setVisible(false);
    }
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
    m_tableProfileChannels->setColumnWidth(2, 115);
    m_tableProfileChannels->setColumnWidth(3, 110);
    m_tableProfileChannels->setColumnWidth(4, 68);
    m_tableProfileChannels->setColumnWidth(5, 96);
    m_tableProfileChannels->setColumnWidth(6, 94);
    m_tableProfileChannels->setColumnWidth(7, 100);
    m_tableProfileChannels->setColumnWidth(8, 105);

    // İstasyon tarafında referans / kalibrasyon alanları gösterilmez (yalnızca Yönetici görür)
    if (!m_isHostMode) {
        m_tableProfileChannels->setColumnHidden(5, true); // Tolerans (dB)
        m_tableProfileChannels->setColumnHidden(6, true); // Golden C/N
        m_tableProfileChannels->setColumnHidden(7, true); // Golden POW
        m_tableProfileChannels->setColumnHidden(8, true); // Golden Durum
    }

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

void ProfileManagerWidget::populateChannelRow(int row, const ChannelConfig &ch) {
    if (!m_tableProfileChannels) return;
    m_tableProfileChannels->setRowHeight(row, 36);

    auto *itNo = new QTableWidgetItem(QString::number(ch.channelNo));
    itNo->setTextAlignment(Qt::AlignCenter);
    itNo->setFlags(itNo->flags() & ~Qt::ItemIsEditable);
    m_tableProfileChannels->setItem(row, 0, itNo);

    m_tableProfileChannels->setItem(row, 1, new QTableWidgetItem(ch.channelName));

    // Frekans (MHz): Sadece geçerli uydu frekans aralığı (950 - 14500 MHz)
    auto *spinFreq = new QSpinBox(m_tableProfileChannels);
    spinFreq->setRange(950, 14500);
    spinFreq->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinFreq->setAlignment(Qt::AlignCenter);
    spinFreq->setSuffix(" MHz");
    spinFreq->setValue(qBound(950, ch.frequencyMhz, 14500));
    spinFreq->setToolTip("Frekans formatı: 950 - 14500 MHz (Örn: 11054)");
    m_tableProfileChannels->setCellWidget(row, 2, spinFreq);

    // Sembol Hızı (kS/s): Sadece geçerli sembol hızı aralığı (1000 - 65000 kS/s)
    auto *spinSr = new QSpinBox(m_tableProfileChannels);
    spinSr->setRange(1000, 65000);
    spinSr->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinSr->setAlignment(Qt::AlignCenter);
    spinSr->setSuffix(" kS/s");
    spinSr->setValue(qBound(1000, ch.symbolRate, 65000));
    spinSr->setToolTip("Sembol hızı formatı: 1000 - 65000 kS/s (Örn: 30000)");
    m_tableProfileChannels->setCellWidget(row, 3, spinSr);

    // Polarizasyon: Yalnızca V veya H seçilebilir
    auto *comboPol = new QComboBox(m_tableProfileChannels);
    comboPol->addItem("V");
    comboPol->addItem("H");
    comboPol->setCurrentIndex(ch.polarization.trimmed().toUpper() == "H" ? 1 : 0);
    comboPol->setToolTip("Polarizasyon: Dikey (V) veya Yatay (H)");
    m_tableProfileChannels->setCellWidget(row, 4, comboPol);

    // Tolerans (dB)
    auto *spinTol = new QDoubleSpinBox(m_tableProfileChannels);
    spinTol->setRange(0.10, 10.00);
    spinTol->setDecimals(2);
    spinTol->setSingleStep(0.10);
    spinTol->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinTol->setAlignment(Qt::AlignCenter);
    spinTol->setSuffix(" dB");
    spinTol->setValue(qBound(0.10, ch.toleranceDb, 10.00));
    m_tableProfileChannels->setCellWidget(row, 5, spinTol);

    // Golden C/N (dB)
    auto *spinGcn = new QDoubleSpinBox(m_tableProfileChannels);
    spinGcn->setRange(0.0, 40.0);
    spinGcn->setDecimals(2);
    spinGcn->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinGcn->setAlignment(Qt::AlignCenter);
    spinGcn->setSuffix(" dB");
    spinGcn->setValue(qBound(0.0, ch.goldenCnDb, 40.0));
    m_tableProfileChannels->setCellWidget(row, 6, spinGcn);

    // Golden Power (dBm)
    auto *spinGpow = new QDoubleSpinBox(m_tableProfileChannels);
    spinGpow->setRange(-120.0, 20.0);
    spinGpow->setDecimals(2);
    spinGpow->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinGpow->setAlignment(Qt::AlignCenter);
    spinGpow->setSuffix(" dBm");
    spinGpow->setValue(qBound(-120.0, ch.goldenPowDbm, 20.0));
    m_tableProfileChannels->setCellWidget(row, 7, spinGpow);

    auto *itStat = new QTableWidgetItem(ch.hasGoldenSample ? "✔ KAYITLI" : "⚪ YOK");
    itStat->setTextAlignment(Qt::AlignCenter);
    itStat->setFlags(itStat->flags() & ~Qt::ItemIsEditable);
    itStat->setForeground(QBrush(QColor(ch.hasGoldenSample ? "#34D399" : "#94A3B8")));
    m_tableProfileChannels->setItem(row, 8, itStat);
}

ChannelConfig ProfileManagerWidget::readChannelFromRow(int row, double fallbackTol) const {
    ChannelConfig ch;
    ch.channelNo = row + 1;
    if (!m_tableProfileChannels || row < 0 || row >= m_tableProfileChannels->rowCount()) {
        return ch;
    }

    auto *itName = m_tableProfileChannels->item(row, 1);
    ch.channelName = itName ? itName->text().trimmed() : QString("Frekans %1").arg(row + 1);
    if (ch.channelName.isEmpty()) {
        ch.channelName = QString("Frekans %1").arg(row + 1);
    }

    if (auto *wFreq = qobject_cast<QSpinBox*>(m_tableProfileChannels->cellWidget(row, 2))) {
        ch.frequencyMhz = wFreq->value();
    } else if (auto *itFreq = m_tableProfileChannels->item(row, 2)) {
        ch.frequencyMhz = itFreq->text().toInt();
    }

    if (auto *wSr = qobject_cast<QSpinBox*>(m_tableProfileChannels->cellWidget(row, 3))) {
        ch.symbolRate = wSr->value();
    } else if (auto *itSr = m_tableProfileChannels->item(row, 3)) {
        ch.symbolRate = itSr->text().toInt();
    }

    if (auto *wPol = qobject_cast<QComboBox*>(m_tableProfileChannels->cellWidget(row, 4))) {
        ch.polarization = wPol->currentText();
    } else if (auto *itPol = m_tableProfileChannels->item(row, 4)) {
        QString pol = itPol->text().trimmed().toUpper();
        ch.polarization = (pol == "H") ? "H" : "V";
    }

    if (auto *wTol = qobject_cast<QDoubleSpinBox*>(m_tableProfileChannels->cellWidget(row, 5))) {
        ch.toleranceDb = wTol->value();
    } else {
        ch.toleranceDb = fallbackTol;
    }

    if (auto *wGcn = qobject_cast<QDoubleSpinBox*>(m_tableProfileChannels->cellWidget(row, 6))) {
        ch.goldenCnDb = wGcn->value();
    }
    if (auto *wGpow = qobject_cast<QDoubleSpinBox*>(m_tableProfileChannels->cellWidget(row, 7))) {
        ch.goldenPowDbm = wGpow->value();
    }

    ch.hasGoldenSample = (ch.goldenCnDb > 0.1 || qAbs(ch.goldenPowDbm) > 0.1);
    return ch;
}

void ProfileManagerWidget::setGoldenCellsOnRow(int row, double cnDb, double powDbm,
                                               const QString &statusText, const QColor &statusColor) {
    if (!m_tableProfileChannels || row < 0 || row >= m_tableProfileChannels->rowCount()) return;
    if (auto *wGcn = qobject_cast<QDoubleSpinBox*>(m_tableProfileChannels->cellWidget(row, 6))) {
        wGcn->setValue(cnDb);
    }
    if (auto *wGpow = qobject_cast<QDoubleSpinBox*>(m_tableProfileChannels->cellWidget(row, 7))) {
        wGpow->setValue(powDbm);
    }
    if (auto *itStat = m_tableProfileChannels->item(row, 8)) {
        itStat->setText(statusText);
        itStat->setForeground(QBrush(statusColor));
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
        populateChannelRow(i, p.channels[i]);
    }
}

void ProfileManagerWidget::onNewProfileClicked() {
    bool ok = false;
    const QString enteredName = QInputDialog::getText(
        this,
        "Yeni Uydu Profili",
        "Profil Adı (zorunlu):",
        QLineEdit::Normal,
        QString(),
        &ok).trimmed();

    if (!ok) {
        return;
    }
    if (enteredName.isEmpty()) {
        QMessageBox::warning(this, "Eksik Bilgi", "Profil ismi girmeden yeni profil oluşturulamaz.");
        return;
    }

    SatelliteProfileInfo p;
    p.id = 0;
    p.profileName = enteredName;
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
    constexpr int kMaxChannels = 50;
    int row = m_tableProfileChannels->rowCount();
    if (row >= kMaxChannels) {
        QMessageBox::information(this, "Kanal Sınırı",
                                 QString("Bir uydu profiline en fazla %1 frekans/kanal eklenebilir.").arg(kMaxChannels));
        return;
    }
    m_tableProfileChannels->insertRow(row);

    ChannelConfig ch;
    ch.channelNo = row + 1;
    ch.channelName = QString("Frekans %1").arg(row + 1);
    ch.frequencyMhz = 11054;
    ch.symbolRate = 30000;
    ch.polarization = "V";
    ch.toleranceDb = m_spinProfTol ? m_spinProfTol->value() : 1.50;
    populateChannelRow(row, ch);

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
    const QVector<ChannelConfig> prevChannels = p.channels;

    p.profileName = m_txtProfName->text().trimmed();
    p.satelliteName = m_txtSatName->text().trimmed();
    p.orbitalPosition = m_txtOrbitalPos->text().trimmed();
    if (m_isHostMode) {
        p.defaultToleranceDb = m_spinProfTol->value();
    }
    p.isActive = true;
    p.channels.clear();

    if (p.profileName.isEmpty()) {
        QMessageBox::warning(this, "Eksik Bilgi", "Profil adı boş olamaz.");
        return;
    }

    for (int r = 0; r < m_tableProfileChannels->rowCount(); ++r) {
        ChannelConfig ch = readChannelFromRow(r, p.defaultToleranceDb);
        // İstasyon düzenlemelerinde yöneticinin kayıtlı tolerans ve Golden Sample verilerini koru
        if (!m_isHostMode && r < prevChannels.size()) {
            ch.toleranceDb = prevChannels[r].toleranceDb;
            ch.goldenCnDb = prevChannels[r].goldenCnDb;
            ch.goldenPowDbm = prevChannels[r].goldenPowDbm;
            ch.hasGoldenSample = prevChannels[r].hasGoldenSample;
        }
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
    ChannelConfig ch = readChannelFromRow(m_goldenCaptureRow, 1.50);

    m_goldenCnSamples.clear();
    m_goldenPowSamples.clear();
    m_lblGoldenCaptureStatus->setText(
        QString("⏳ Ölçülüyor [%1/%2]: %3 (%4 MHz %5)...")
            .arg(m_goldenCaptureRow + 1)
            .arg(m_tableProfileChannels->rowCount())
            .arg(ch.channelName).arg(ch.frequencyMhz).arg(ch.polarization));

    if (m_refWebClient && m_refWebClient->isReachable()) {
        m_refWebClient->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        QTimer::singleShot(900, this, [this]() { if (m_goldenCaptureActive && m_refWebClient) m_refWebClient->requestStatus(); });
        QTimer::singleShot(1600, this, [this]() { if (m_goldenCaptureActive && m_refWebClient) m_refWebClient->requestStatus(); });
    }
    if (m_refSerial && m_refSerial->isConnected()) {
        m_refSerial->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        QTimer::singleShot(1000, this, [this]() { if (m_goldenCaptureActive && m_refSerial) m_refSerial->requestStatus(); });
    }
    m_goldenStepTimer.start(2400);
}

void ProfileManagerWidget::onGoldenCaptureStepTimeout() {
    if (!m_goldenCaptureActive || !m_tableProfileChannels) return;

    if (!m_goldenCnSamples.isEmpty()) {
        double avgCn = std::accumulate(m_goldenCnSamples.begin(), m_goldenCnSamples.end(), 0.0) / m_goldenCnSamples.size();
        double avgPow = std::accumulate(m_goldenPowSamples.begin(), m_goldenPowSamples.end(), 0.0) / m_goldenPowSamples.size();
        setGoldenCellsOnRow(m_goldenCaptureRow, avgCn, avgPow, "✔ ÖLÇÜLDÜ", QColor("#34D399"));

        int chNo = m_goldenCaptureRow + 1;
        DbManager::instance().updateChannelGoldenSample(m_goldenCaptureProfileId, chNo, avgCn, avgPow);
        emit logMessage(QString("[Golden Sample] CH%1 kaydedildi: C/N=%2 dB | POW=%3 dBm").arg(chNo).arg(avgCn, 0, 'f', 2).arg(avgPow, 0, 'f', 2));
    } else {
        if (auto *itStat = m_tableProfileChannels->item(m_goldenCaptureRow, 8)) {
            itStat->setText("⚠ VERİ YOK");
            itStat->setForeground(QBrush(QColor("#FBBF24")));
        }
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
