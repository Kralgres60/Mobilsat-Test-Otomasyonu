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
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QAction>
#include <QKeyEvent>
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

    setReceiverClients(nullptr, m_refWebClient, m_refSerial);
}

void ProfileManagerWidget::setReceiverClients(DrxWebClient *dutWeb, DrxWebClient *refWeb, SerialWorker *refSerial) {
    m_dutWebClient = dutWeb;
    m_refWebClient = refWeb;
    m_refSerial = refSerial;

    if (m_comboReceiverSource) {
        m_comboReceiverSource->blockSignals(true);
        m_comboReceiverSource->clear();
        if (m_dutWebClient) {
            m_comboReceiverSource->addItem("📡 DUT DRX (İstasyon)", 0);
        }
        if (m_refWebClient) {
            m_comboReceiverSource->addItem("🛰️ Referans DRX (Golden)", 1);
        }
        if (m_comboReceiverSource->count() == 0) {
            m_comboReceiverSource->addItem("📡 DRX Alıcısı", 0);
        }
        m_comboReceiverSource->blockSignals(false);
    }

    if (m_dutWebClient) {
        disconnect(m_dutWebClient, &DrxWebClient::telemetryReceived, this, nullptr);
        connect(m_dutWebClient, &DrxWebClient::telemetryReceived, this,
                [this](double powDbm, double cnDb, double, double) {
            onReferenceTelemetryReceived(powDbm, cnDb);
        });
    }
    if (m_refWebClient) {
        disconnect(m_refWebClient, &DrxWebClient::telemetryReceived, this, nullptr);
        connect(m_refWebClient, &DrxWebClient::telemetryReceived, this,
                [this](double powDbm, double cnDb, double, double) {
            onReferenceTelemetryReceived(powDbm, cnDb);
        });
    }
    if (m_refSerial) {
        disconnect(m_refSerial, &SerialWorker::telemetryReceived, this, nullptr);
        connect(m_refSerial, &SerialWorker::telemetryReceived, this,
                [this](double powDbm, double cnDb, double) {
            onReferenceTelemetryReceived(powDbm, cnDb);
        });
    }
}

DrxWebClient* ProfileManagerWidget::activeCaptureClient() const {
    if (m_comboReceiverSource && m_comboReceiverSource->count() > 0) {
        int idx = m_comboReceiverSource->currentData().toInt();
        if (idx == 0 && m_dutWebClient) return m_dutWebClient;
        if (idx == 1 && m_refWebClient) return m_refWebClient;
    }
    if (m_refWebClient && m_refWebClient->isReachable()) return m_refWebClient;
    if (m_dutWebClient && m_dutWebClient->isReachable()) return m_dutWebClient;
    return m_dutWebClient ? m_dutWebClient : m_refWebClient;
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
    grpProfLeft->setFixedWidth(300);
    QVBoxLayout *leftVBox = new QVBoxLayout(grpProfLeft);
    leftVBox->setSpacing(8);

    m_listProfiles = new QListWidget(grpProfLeft);
    m_listProfiles->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listProfiles->installEventFilter(this);
    connect(m_listProfiles, &QListWidget::currentRowChanged, this, &ProfileManagerWidget::onProfileSelected);
    connect(m_listProfiles, &QListWidget::customContextMenuRequested, this, &ProfileManagerWidget::showProfileContextMenu);
    connect(m_listProfiles, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        onEditProfilePopup(m_listProfiles->currentRow());
    });
    leftVBox->addWidget(m_listProfiles, 1);

    QPushButton *btnNewProf = new QPushButton("➕ Yeni Profil", grpProfLeft);
    btnNewProf->setCursor(Qt::PointingHandCursor);
    btnNewProf->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 7px 12px; font-weight: bold;");
    connect(btnNewProf, &QPushButton::clicked, this, &ProfileManagerWidget::onNewProfileClicked);
    leftVBox->addWidget(btnNewProf);

    splitHBox->addWidget(grpProfLeft);

    // Right Column: Frequency and Golden Sample table
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
    m_tableProfileChannels->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableProfileChannels->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableProfileChannels->installEventFilter(this);
    m_tableProfileChannels->viewport()->installEventFilter(this);
    m_tableProfileChannels->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    connect(m_tableProfileChannels, &QTableWidget::customContextMenuRequested, this, &ProfileManagerWidget::showChannelContextMenu);
    connect(m_tableProfileChannels, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (!m_isPopulating && it && it->column() == 1) {
            autoSaveCurrentProfile();
        }
    });

    chVBox->addWidget(m_tableProfileChannels, 1);

    QHBoxLayout *chActionRow = new QHBoxLayout();
    QPushButton *btnAddCh = new QPushButton("➕ Frekans Ekle", grpChannels);
    btnAddCh->setCursor(Qt::PointingHandCursor);
    connect(btnAddCh, &QPushButton::clicked, this, &ProfileManagerWidget::onAddChannelRowClicked);

    chActionRow->addWidget(btnAddCh);
    chActionRow->addStretch();
    chVBox->addLayout(chActionRow);

    m_lblGoldenCaptureStatus = new QLabel(
        "İpucu: Profil ve frekans değişiklikleri otomatik kaydedilir. Profili düzenlemek için çift tıklayın; silmek için Sağ Tık veya Delete tuşunu kullanın.",
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
    if (selectProfileId <= 0 && m_listProfiles->currentRow() >= 0 && m_listProfiles->currentRow() < m_profiles.size()) {
        selectProfileId = m_profiles[m_listProfiles->currentRow()].id;
    }
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

    // Frequency (MHz): Valid satellite RF frequency range (950 - 14500 MHz)
    auto *spinFreq = new QSpinBox(m_tableProfileChannels);
    spinFreq->setRange(950, 14500);
    spinFreq->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinFreq->setAlignment(Qt::AlignCenter);
    spinFreq->setSuffix(" MHz");
    spinFreq->setValue(qBound(950, ch.frequencyMhz, 14500));
    spinFreq->setToolTip("Frekans formatı: 950 - 14500 MHz (Örn: 11054)");
    connect(spinFreq, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { autoSaveCurrentProfile(); });
    m_tableProfileChannels->setCellWidget(row, 2, spinFreq);

    // Symbol Rate (kS/s): Valid symbol rate range (1000 - 65000 kS/s)
    auto *spinSr = new QSpinBox(m_tableProfileChannels);
    spinSr->setRange(1000, 65000);
    spinSr->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinSr->setAlignment(Qt::AlignCenter);
    spinSr->setSuffix(" kS/s");
    spinSr->setValue(qBound(1000, ch.symbolRate, 65000));
    spinSr->setToolTip("Sembol hızı formatı: 1000 - 65000 kS/s (Örn: 30000)");
    connect(spinSr, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { autoSaveCurrentProfile(); });
    m_tableProfileChannels->setCellWidget(row, 3, spinSr);

    // Polarization: Only V or H
    auto *comboPol = new QComboBox(m_tableProfileChannels);
    comboPol->addItem("V");
    comboPol->addItem("H");
    comboPol->setCurrentIndex(ch.polarization.trimmed().toUpper() == "H" ? 1 : 0);
    comboPol->setToolTip("Polarizasyon: Dikey (V) veya Yatay (H)");
    connect(comboPol, &QComboBox::currentTextChanged, this, [this](const QString &) { autoSaveCurrentProfile(); });
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
    connect(spinTol, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { autoSaveCurrentProfile(); });
    m_tableProfileChannels->setCellWidget(row, 5, spinTol);

    // Golden C/N (dB)
    auto *spinGcn = new QDoubleSpinBox(m_tableProfileChannels);
    spinGcn->setRange(0.0, 40.0);
    spinGcn->setDecimals(2);
    spinGcn->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinGcn->setAlignment(Qt::AlignCenter);
    spinGcn->setSuffix(" dB");
    spinGcn->setValue(qBound(0.0, ch.goldenCnDb, 40.0));
    connect(spinGcn, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { autoSaveCurrentProfile(); });
    m_tableProfileChannels->setCellWidget(row, 6, spinGcn);

    // Golden Power (dBm)
    auto *spinGpow = new QDoubleSpinBox(m_tableProfileChannels);
    spinGpow->setRange(-120.0, 20.0);
    spinGpow->setDecimals(2);
    spinGpow->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinGpow->setAlignment(Qt::AlignCenter);
    spinGpow->setSuffix(" dBm");
    spinGpow->setValue(qBound(-120.0, ch.goldenPowDbm, 20.0));
    connect(spinGpow, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { autoSaveCurrentProfile(); });
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
    ch.channelName = itName ? itName->text().trimmed() : QString("Kanal %1").arg(row + 1);
    if (ch.channelName.isEmpty()) {
        ch.channelName = QString("Kanal %1").arg(row + 1);
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

    m_isPopulating = true;
    m_tableProfileChannels->setRowCount(p.channels.size());
    for (int i = 0; i < p.channels.size(); ++i) {
        populateChannelRow(i, p.channels[i]);
    }
    m_tableProfileChannels->resizeColumnsToContents();
    for (int c = 0; c < m_tableProfileChannels->columnCount() - 1; ++c) {
        m_tableProfileChannels->setColumnWidth(c, m_tableProfileChannels->columnWidth(c) + 14);
    }
    m_isPopulating = false;
}

static bool execProfileMetaDialog(QWidget *parent,
                                  const QString &title,
                                  bool showTolerance,
                                  SatelliteProfileInfo &profile) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(420);

    auto *lay = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout();
    form->setSpacing(10);

    auto *txtName = new QLineEdit(profile.profileName, &dlg);
    txtName->setPlaceholderText("Örn: Türksat 42E - Genel");

    auto *txtSat = new QLineEdit(profile.satelliteName, &dlg);
    txtSat->setPlaceholderText("Örn: Türksat 42.0°E");

    auto *txtOrb = new QLineEdit(profile.orbitalPosition, &dlg);
    txtOrb->setPlaceholderText("Örn: 42.0E");

    form->addRow("Profil Adı (*):", txtName);
    form->addRow("Uydu Adı:", txtSat);
    form->addRow("Yörünge:", txtOrb);

    QDoubleSpinBox *spinTol = nullptr;
    if (showTolerance) {
        spinTol = new QDoubleSpinBox(&dlg);
        spinTol->setRange(0.1, 20.0);
        spinTol->setDecimals(2);
        spinTol->setSuffix(" dB");
        spinTol->setValue(profile.defaultToleranceDb > 0.0 ? profile.defaultToleranceDb : AppConfig::instance().defaultToleranceDb());
        form->addRow("Varsayılan Tolerans:", spinTol);
    }

    lay->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText("Kaydet");
    buttons->button(QDialogButtonBox::Cancel)->setText("İptal");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        if (txtName->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, "Eksik Bilgi", "Profil adı boş olamaz.");
            txtName->setFocus();
            return;
        }
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    profile.profileName = txtName->text().trimmed();
    profile.satelliteName = txtSat->text().trimmed().isEmpty() ? QString("Türksat 42.0°E") : txtSat->text().trimmed();
    profile.orbitalPosition = txtOrb->text().trimmed().isEmpty() ? QString("42.0E") : txtOrb->text().trimmed();
    if (spinTol) {
        profile.defaultToleranceDb = spinTol->value();
    }
    return true;
}

void ProfileManagerWidget::onNewProfileClicked() {
    SatelliteProfileInfo p;
    p.id = 0;
    p.satelliteName = "Türksat 42.0°E";
    p.orbitalPosition = "42.0E";
    p.defaultToleranceDb = AppConfig::instance().defaultToleranceDb();
    p.isActive = true;

    if (!execProfileMetaDialog(this, "Yeni Uydu Profili Ekle", m_isHostMode, p)) {
        return;
    }

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

void ProfileManagerWidget::onEditProfilePopup(int row) {
    if (row < 0 || row >= m_profiles.size()) return;
    SatelliteProfileInfo p = m_profiles[row];
    if (!execProfileMetaDialog(this, "Uydu Profili Düzenle", m_isHostMode, p)) {
        return;
    }
    if (DbManager::instance().saveProfile(p)) {
        reloadProfiles(p.id);
        emit logMessage("[Profiller] Uydu profili güncellendi: " + p.profileName);
        emit profilesChanged();
    } else {
        QMessageBox::warning(this, "Kayıt Hatası", "Profil güncellenemedi:\n" + DbManager::instance().lastError());
    }
}

void ProfileManagerWidget::showProfileContextMenu(const QPoint &pos) {
    if (!m_listProfiles) return;
    QListWidgetItem *item = m_listProfiles->itemAt(pos);
    if (!item) return;
    int row = m_listProfiles->row(item);
    m_listProfiles->setCurrentRow(row);

    QMenu menu(this);
    QAction *actEdit = menu.addAction("✏️ Profili Düzenle");
    QAction *actDel = menu.addAction("🗑️ Profili Sil");
    if (row >= 0 && row < m_profiles.size() && m_profiles[row].id <= 1) {
        actDel->setEnabled(false);
    }

    QAction *chosen = menu.exec(m_listProfiles->viewport()->mapToGlobal(pos));
    if (chosen == actEdit) {
        onEditProfilePopup(row);
    } else if (chosen == actDel) {
        onDeleteProfileClicked();
    }
}

void ProfileManagerWidget::onDeleteProfileClicked() {
    int row = m_listProfiles ? m_listProfiles->currentRow() : -1;
    if (row < 0 || row >= m_profiles.size()) return;
    int pid = m_profiles[row].id;
    const QString name = m_profiles[row].profileName;
    if (pid <= 1) {
        QMessageBox::information(this, "Korunan Profil", "Varsayılan fabrika profili (ID 1) silinemez.");
        return;
    }
    if (QMessageBox::question(this,
                              "Profili Sil",
                              QString("'%1' uydu profilini ve tüm kanallarını silmek istediğinize emin misiniz?").arg(name),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
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

    int profRow = m_listProfiles ? m_listProfiles->currentRow() : -1;
    double fallbackTol = (profRow >= 0 && profRow < m_profiles.size())
                             ? m_profiles[profRow].defaultToleranceDb
                             : AppConfig::instance().defaultToleranceDb();

    m_isPopulating = true;
    m_tableProfileChannels->insertRow(row);

    ChannelConfig ch;
    ch.channelNo = row + 1;
    ch.channelName = QString("Kanal %1").arg(row + 1);
    ch.frequencyMhz = 11054;
    ch.symbolRate = 30000;
    ch.polarization = "V";
    ch.toleranceDb = fallbackTol;
    populateChannelRow(row, ch);
    m_isPopulating = false;

    m_tableProfileChannels->scrollToBottom();
    autoSaveCurrentProfile(false);
}

void ProfileManagerWidget::onRemoveChannelRowClicked() {
    if (!m_tableProfileChannels) return;
    int rows = m_tableProfileChannels->rowCount();
    if (rows <= 1) {
        QMessageBox::information(this, "Kanal Silinemedi", "Her uydu profilinde en az 1 kanal bulunmalıdır.");
        return;
    }

    int selRow = m_tableProfileChannels->currentRow();
    int removeIdx = (selRow >= 0 && selRow < rows) ? selRow : (rows - 1);

    m_isPopulating = true;
    m_tableProfileChannels->removeRow(removeIdx);

    // Re-number channel sequence indices cleanly
    for (int r = 0; r < m_tableProfileChannels->rowCount(); ++r) {
        if (auto *it = m_tableProfileChannels->item(r, 0)) {
            it->setText(QString::number(r + 1));
        }
    }
    m_isPopulating = false;

    autoSaveCurrentProfile(false);
}

void ProfileManagerWidget::showChannelContextMenu(const QPoint &pos) {
    if (!m_tableProfileChannels) return;
    int row = m_tableProfileChannels->rowAt(pos.y());
    if (row >= 0) {
        m_tableProfileChannels->selectRow(row);
    }

    QMenu menu(this);
    QAction *actAdd = menu.addAction("➕ Yeni Kanal Ekle");
    QAction *actDel = menu.addAction("🗑️ Seçili Kanalı Sil");
    actDel->setEnabled(row >= 0 && m_tableProfileChannels->rowCount() > 1);

    QAction *chosen = menu.exec(m_tableProfileChannels->viewport()->mapToGlobal(pos));
    if (chosen == actAdd) {
        onAddChannelRowClicked();
    } else if (chosen == actDel) {
        onRemoveChannelRowClicked();
    }
}

bool ProfileManagerWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Delete) {
            if (watched == m_listProfiles) {
                onDeleteProfileClicked();
                return true;
            }
            if (watched == m_tableProfileChannels) {
                onRemoveChannelRowClicked();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProfileManagerWidget::autoSaveCurrentProfile(bool reloadList) {
    if (m_isPopulating || !m_listProfiles || !m_tableProfileChannels) return;
    int row = m_listProfiles->currentRow();
    if (row < 0 || row >= m_profiles.size()) return;

    SatelliteProfileInfo p = m_profiles[row];
    p.isActive = true;
    p.channels.clear();
    for (int r = 0; r < m_tableProfileChannels->rowCount(); ++r) {
        p.channels.append(readChannelFromRow(r, p.defaultToleranceDb));
    }

    if (DbManager::instance().saveProfile(p)) {
        m_profiles[row] = p;
        if (reloadList) {
            reloadProfiles(p.id);
        }
        emit profilesChanged();
    } else {
        emit logMessage("[Profiller] Otomatik kayıt hatası: " + DbManager::instance().lastError());
    }
}

void ProfileManagerWidget::onSaveProfileClicked() {
    autoSaveCurrentProfile(true);
}

void ProfileManagerWidget::onReferenceTelemetryReceived(double powDbm, double cnDb) {
    if (m_goldenCaptureActive) {
        m_goldenCnSamples.append(cnDb);
        m_goldenPowSamples.append(powDbm);
    }
}

void ProfileManagerWidget::onStartGoldenCaptureClicked() {
    if (m_goldenCaptureActive) {
        m_goldenCaptureActive = false;
        m_goldenStepTimer.stop();
        m_btnGoldenCapture->setText("📸 Golden Sample Ölçümü Al");
        m_btnGoldenCapture->setStyleSheet("background-color: #D97706; color: #FFFFFF; border: none; border-radius: 4px; padding: 7px 12px; font-weight: bold;");
        m_lblGoldenCaptureStatus->setText("Golden Sample ölçümü kullanıcı tarafından durduruldu.");
        return;
    }

    int row = m_listProfiles ? m_listProfiles->currentRow() : -1;
    if (row < 0 || row >= m_profiles.size()) return;

    DrxWebClient *client = activeCaptureClient();
    bool refReady = (client && client->isReachable()) ||
                    (m_refSerial && m_refSerial->isConnected());
    if (!refReady) {
        QMessageBox::warning(this, "DRX Alıcısı Bağlı Değil",
                             "Golden Sample ölçümü alabilmek için seçilen DRX alıcısının (DUT veya Referans) Ethernet veya RS232 üzerinden bağlı olması gerekir.\n\n"
                             "İpucu: Canlı cihaz bağlı değilse tablodaki 'Golden C/N' ve 'Golden POW' sütunlarına doğrudan sabit değer girip kaydedebilirsiniz.");
        return;
    }

    if (m_tableProfileChannels->rowCount() == 0) return;

    m_goldenCaptureActive = true;
    m_goldenCaptureProfileId = m_profiles[row].id;
    m_goldenCaptureRow = 0;
    m_btnGoldenCapture->setText("⏹ Ölçümü Durdur");
    m_btnGoldenCapture->setStyleSheet("background-color: #DC2626; color: #FFFFFF; border: none; border-radius: 4px; padding: 7px 12px; font-weight: bold;");
    triggerGoldenCaptureCurrentRow();
}

void ProfileManagerWidget::triggerGoldenCaptureCurrentRow() {
    if (!m_goldenCaptureActive || !m_tableProfileChannels) return;
    ChannelConfig ch = readChannelFromRow(m_goldenCaptureRow, 1.50);

    m_goldenCnSamples.clear();
    m_goldenPowSamples.clear();
    const int sampleSec = m_spinSampleDuration ? m_spinSampleDuration->value() : 5;
    m_lblGoldenCaptureStatus->setText(
        QString("⏳ Ölçülüyor [%1/%2]: %3 (%4 MHz %5) - %6 sn örnekleniyor...")
            .arg(m_goldenCaptureRow + 1)
            .arg(m_tableProfileChannels->rowCount())
            .arg(ch.channelName).arg(ch.frequencyMhz).arg(ch.polarization)
            .arg(sampleSec));

    setGoldenCellsOnRow(m_goldenCaptureRow, 0.0, 0.0, "ÖLÇÜLÜYOR...", QColor("#F59E0B"));

    DrxWebClient *client = activeCaptureClient();
    if (client && client->isReachable()) {
        client->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        for (int t = 800; t < sampleSec * 1000; t += 450) {
            QTimer::singleShot(t, this, [this, client]() {
                if (m_goldenCaptureActive && client && client->isReachable()) {
                    client->requestStatus();
                }
            });
        }
    } else if (m_refSerial && m_refSerial->isConnected()) {
        m_refSerial->sendTune(ch.frequencyMhz, ch.symbolRate, ch.polarization);
        for (int t = 800; t < sampleSec * 1000; t += 450) {
            QTimer::singleShot(t, this, [this]() {
                if (m_goldenCaptureActive && m_refSerial && m_refSerial->isConnected()) {
                    m_refSerial->requestStatus();
                }
            });
        }
    }
    m_goldenStepTimer.start(sampleSec * 1000);
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
        m_btnGoldenCapture->setText("📸 Golden Sample Ölçümü Al");
        m_btnGoldenCapture->setStyleSheet("background-color: #D97706; color: #FFFFFF; border: none; border-radius: 4px; padding: 7px 12px; font-weight: bold;");
        m_lblGoldenCaptureStatus->setText("✔ Tüm kanallar için Golden Sample ölçümü tamamlandı ve veritabanına kaydedildi!");
        reloadProfiles(m_goldenCaptureProfileId);
        emit profilesChanged();
        QMessageBox::information(this, "Golden Sample Tamamlandı",
                                 "Seçili uydu profilindeki tüm kanallar için Golden Sample referans ölçümleri tamamlandı ve kaydedildi.");
    }
}
