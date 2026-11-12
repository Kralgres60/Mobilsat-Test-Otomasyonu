/**
 * @file simulator_window.cpp
 * @brief Implementation of the interactive TCP DRX9000 telemetry curve editor and network transmitter GUI.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "simulator_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QDateTime>
#include <QFileInfo>
#include <QAbstractItemView>
#include <QSignalBlocker>

SimulatorWindow::SimulatorWindow(QWidget *parent)
    : QMainWindow(parent),
      m_server(new DrxSimServer("DRX-SIM-01", "00:1A:2B:90:00:01", this)),
      m_playTimer(new QTimer(this)) {

    m_points = SimCurveModel::createPreset(SimCurveModel::PresetType::NormalStable, 30);

    setupUi();
    m_comboBindIp->addItem(QStringLiteral("0.0.0.0 (Tüm Ağ Arayüzleri / LAN)"), QStringLiteral("0.0.0.0"));
    m_curveWidget->setCurveData(&m_points);
    rebuildTable();
    setActivePlayIndex(0);
    m_lblEndpoint->setText(QStringLiteral("Ağ arayüzleri tespit ediliyor..."));

    // Defer Windows NIC enumeration so the window displays immediately
    QTimer::singleShot(0, this, [this]() {
        populateBindAddresses();
        refreshEndpointHint();
    });

    connect(m_server, &DrxSimServer::logMessage, this, &SimulatorWindow::appendLog);
    connect(m_server, &DrxSimServer::requestHandled, this, &SimulatorWindow::onRequestHandled);
    connect(m_server, &DrxSimServer::tuned, this, &SimulatorWindow::onServerTuned);

    connect(m_playTimer, &QTimer::timeout, this, &SimulatorWindow::onPlaybackTick);
    m_playTimer->setInterval(m_spinInterval->value());
    m_playTimer->start();

    appendLog(QStringLiteral("Simülatör hazır. Grafik üzerinde nokta çizip 'TCP Yayını Başlat' butonuna basınız."));
}

SimulatorWindow::~SimulatorWindow() {
    if (m_server) {
        m_server->stop();
    }
}

void SimulatorWindow::setupUi() {
    setWindowTitle(QStringLiteral("NETA DRX9000 TCP Telemetri & Eğri Simülatörü"));
    resize(1240, 820);

    setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #0F172A;
            color: #F1F5F9;
        }
        QGroupBox {
            border: 1px solid #1E293B;
            border-radius: 6px;
            margin-top: 10px;
            padding-top: 8px;
            font-weight: bold;
            color: #10B981;
            background-color: #111827;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
        }
        QLineEdit, QSpinBox, QComboBox {
            background-color: #1E293B;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 4px 8px;
            color: #F8FAFC;
        }
        QPushButton {
            background-color: #1E293B;
            border: 1px solid #334155;
            border-radius: 4px;
            padding: 5px 11px;
            font-weight: bold;
            color: #E2E8F0;
        }
        QPushButton:hover {
            background-color: #334155;
            border-color: #10B981;
        }
        QPushButton:checked {
            background-color: #065F46;
            border-color: #10B981;
            color: #FFFFFF;
        }
        QTableWidget {
            background-color: #0B1120;
            alternate-background-color: #111827;
            gridline-color: #1E293B;
            border: 1px solid #1E293B;
            border-radius: 4px;
            selection-background-color: #065F46;
            selection-color: #FFFFFF;
        }
        QHeaderView::section {
            background-color: #1E293B;
            color: #94A3B8;
            padding: 4px;
            border: none;
            border-right: 1px solid #334155;
            font-weight: bold;
        }
        QTextEdit {
            background-color: #020617;
            border: 1px solid #1E293B;
            border-radius: 4px;
            font-family: Consolas, monospace;
            font-size: 8.5pt;
            color: #CBD5E1;
        }
    )");

    auto *central = new QWidget(this);
    auto *rootLay = new QVBoxLayout(central);
    rootLay->setContentsMargins(10, 8, 10, 10);
    rootLay->setSpacing(8);

    // =========================================================================
    // 1. TCP NETWORK & SERVER BAR
    // =========================================================================
    auto *netGroup = new QGroupBox(QStringLiteral("TCP Yayın Ayarları (Çoklu PC Veri Toplama)"), central);
    auto *netLay = new QHBoxLayout(netGroup);
    netLay->setSpacing(8);

    netLay->addWidget(new QLabel(QStringLiteral("Dinlenecek Arayüz (IP):")));
    m_comboBindIp = new QComboBox(netGroup);
    m_comboBindIp->setMinimumWidth(210);
    connect(m_comboBindIp, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulatorWindow::refreshEndpointHint);
    netLay->addWidget(m_comboBindIp);

    netLay->addWidget(new QLabel(QStringLiteral("TCP Port:")));
    m_spinPort = new QSpinBox(netGroup);
    m_spinPort->setRange(1, 65535);
    m_spinPort->setValue(80);
    m_spinPort->setFixedWidth(78);
    connect(m_spinPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &SimulatorWindow::refreshEndpointHint);
    netLay->addWidget(m_spinPort);

    m_btnStartStop = new QPushButton(QStringLiteral("TCP Yayını Başlat"), netGroup);
    m_btnStartStop->setMinimumWidth(145);
    m_btnStartStop->setStyleSheet("QPushButton { background-color: #059669; border-color: #10B981; color: white; } QPushButton:hover { background-color: #10B981; }");
    connect(m_btnStartStop, &QPushButton::clicked, this, &SimulatorWindow::onToggleServer);
    netLay->addWidget(m_btnStartStop);

    m_lblTcpStatus = new QLabel(QStringLiteral("DURDURULDU"), netGroup);
    m_lblTcpStatus->setAlignment(Qt::AlignCenter);
    m_lblTcpStatus->setFixedWidth(105);
    m_lblTcpStatus->setStyleSheet("background-color: #334155; color: #CBD5E1; border-radius: 4px; padding: 4px; font-weight: bold;");
    netLay->addWidget(m_lblTcpStatus);

    m_lblEndpoint = new QLabel(netGroup);
    m_lblEndpoint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lblEndpoint->setStyleSheet("color: #38BDF8; font-weight: bold; padding-left: 6px;");
    netLay->addWidget(m_lblEndpoint, 1);

    rootLay->addWidget(netGroup);

    // =========================================================================
    // 2. CURVE EDITOR & PLAYBACK TOOLBAR
    // =========================================================================
    auto *toolGroup = new QGroupBox(QStringLiteral("Etkileşimli Grafik Editörü & Zaman Akışı"), central);
    auto *toolVBox = new QVBoxLayout(toolGroup);
    toolVBox->setSpacing(6);

    auto *barRow1 = new QHBoxLayout();
    barRow1->addWidget(new QLabel(QStringLiteral("Fare ile Çiz:")));

    m_btnEditCn = new QPushButton(QStringLiteral("C/N (dB)"), toolGroup);
    m_btnEditCn->setCheckable(true);
    m_btnEditCn->setChecked(true);
    barRow1->addWidget(m_btnEditCn);

    m_btnEditPower = new QPushButton(QStringLiteral("Güç (dBm)"), toolGroup);
    m_btnEditPower->setCheckable(true);
    barRow1->addWidget(m_btnEditPower);

    connect(m_btnEditCn, &QPushButton::clicked, this, [this]() {
        m_btnEditCn->setChecked(true);
        m_btnEditPower->setChecked(false);
        m_curveWidget->setEditTarget(SimCurveWidget::EditTarget::CnCurve);
    });
    connect(m_btnEditPower, &QPushButton::clicked, this, [this]() {
        m_btnEditPower->setChecked(true);
        m_btnEditCn->setChecked(false);
        m_curveWidget->setEditTarget(SimCurveWidget::EditTarget::PowerCurve);
    });

    barRow1->addSpacing(10);
    barRow1->addWidget(new QLabel(QStringLiteral("Hazır Senaryo:")));
    m_comboPreset = new QComboBox(toolGroup);
    m_comboPreset->addItem(QStringLiteral("Normal Kararlı (PASS)"), static_cast<int>(SimCurveModel::PresetType::NormalStable));
    m_comboPreset->addItem(QStringLiteral("Ortada C/N Düşüşü (FAIL Testi)"), static_cast<int>(SimCurveModel::PresetType::MiddleCnDropFault));
    m_comboPreset->addItem(QStringLiteral("Kısa Sinyal Kopması (LOCK Loss)"), static_cast<int>(SimCurveModel::PresetType::BriefSignalLoss));
    m_comboPreset->addItem(QStringLiteral("Kademeli Sinyal Zayıflaması"), static_cast<int>(SimCurveModel::PresetType::SteppedDecay));
    barRow1->addWidget(m_comboPreset);

    auto *btnApplyPreset = new QPushButton(QStringLiteral("Şablonu Yükle"), toolGroup);
    connect(btnApplyPreset, &QPushButton::clicked, this, &SimulatorWindow::onApplyPreset);
    barRow1->addWidget(btnApplyPreset);

    auto *btnAddStep = new QPushButton(QStringLiteral("+ Adım"), toolGroup);
    connect(btnAddStep, &QPushButton::clicked, this, &SimulatorWindow::onAddStep);
    barRow1->addWidget(btnAddStep);

    auto *btnRemoveStep = new QPushButton(QStringLiteral("- Sonu Sil"), toolGroup);
    connect(btnRemoveStep, &QPushButton::clicked, this, &SimulatorWindow::onRemoveLastStep);
    barRow1->addWidget(btnRemoveStep);

    barRow1->addStretch();

    auto *btnImport = new QPushButton(QStringLiteral("İçe Aktar (JSON/CSV)"), toolGroup);
    btnImport->setStyleSheet("QPushButton { background-color: #1D4ED8; border-color: #3B82F6; color: white; }");
    connect(btnImport, &QPushButton::clicked, this, &SimulatorWindow::onImportCurve);
    barRow1->addWidget(btnImport);

    auto *btnExport = new QPushButton(QStringLiteral("Dışa Aktar (JSON/CSV)"), toolGroup);
    btnExport->setStyleSheet("QPushButton { background-color: #0369A1; border-color: #0EA5E9; color: white; }");
    connect(btnExport, &QPushButton::clicked, this, &SimulatorWindow::onExportCurve);
    barRow1->addWidget(btnExport);

    toolVBox->addLayout(barRow1);

    // Row 2: Playback & Interactive Curve Widget
    auto *barRow2 = new QHBoxLayout();
    m_btnPlayPause = new QPushButton(QStringLiteral("Akışı Duraklat"), toolGroup);
    m_btnPlayPause->setCheckable(true);
    m_btnPlayPause->setChecked(true);
    connect(m_btnPlayPause, &QPushButton::clicked, this, &SimulatorWindow::onPlayPause);
    barRow2->addWidget(m_btnPlayPause);

    auto *btnRewind = new QPushButton(QStringLiteral("Başa Sar (#0)"), toolGroup);
    connect(btnRewind, &QPushButton::clicked, this, &SimulatorWindow::onRewindPlayback);
    barRow2->addWidget(btnRewind);

    barRow2->addWidget(new QLabel(QStringLiteral("Adım Hızı (ms):")));
    m_spinInterval = new QSpinBox(toolGroup);
    m_spinInterval->setRange(100, 5000);
    m_spinInterval->setSingleStep(50);
    m_spinInterval->setValue(250);
    connect(m_spinInterval, QOverload<int>::of(&QSpinBox::valueChanged),
            m_playTimer, QOverload<int>::of(&QTimer::setInterval));
    barRow2->addWidget(m_spinInterval);

    m_chkLoop = new QCheckBox(QStringLiteral("Döngü (Loop)"), toolGroup);
    m_chkLoop->setChecked(true);
    barRow2->addWidget(m_chkLoop);

    m_chkNoise = new QCheckBox(QStringLiteral("RF Gürültü (±0.15 dB)"), toolGroup);
    m_chkNoise->setChecked(false);
    connect(m_chkNoise, &QCheckBox::toggled, m_server, &DrxSimServer::setNoiseEnabled);
    barRow2->addWidget(m_chkNoise);

    barRow2->addStretch();

    auto *btnExpireSession = new QPushButton(QStringLiteral("SessionKey Sıfırla (Re-Login Testi)"), toolGroup);
    connect(btnExpireSession, &QPushButton::clicked, m_server, &DrxSimServer::expireSessionKey);
    barRow2->addWidget(btnExpireSession);

    toolVBox->addLayout(barRow2);

    m_curveWidget = new SimCurveWidget(toolGroup);
    connect(m_curveWidget, &SimCurveWidget::pointEdited, this, &SimulatorWindow::onCurvePointEdited);
    connect(m_curveWidget, &SimCurveWidget::selectedIndexChanged, this, &SimulatorWindow::onGraphStepSelected);
    toolVBox->addWidget(m_curveWidget);

    rootLay->addWidget(toolGroup, 3);

    // =========================================================================
    // 3. BOTTOM SPLIT: STEP TABLE + FAULT INJECTOR  |  LIVE TELEMETRY & LOG
    // =========================================================================
    auto *splitter = new QSplitter(Qt::Horizontal, central);

    // Left: Step table & quick fault actions
    auto *tableGroup = new QGroupBox(QStringLiteral("Adım Değerleri & Seçili Nokta Hata Enjeksiyonu"), splitter);
    auto *tableLay = new QVBoxLayout(tableGroup);

    auto *faultBar = new QHBoxLayout();
    auto *btnLowCn = new QPushButton(QStringLiteral("Seçiliye Düşük C/N (7.2 dB)"), tableGroup);
    btnLowCn->setStyleSheet("QPushButton { background-color: #92400E; border-color: #F59E0B; color: white; }");
    connect(btnLowCn, &QPushButton::clicked, this, &SimulatorWindow::onInjectLowCnFault);
    faultBar->addWidget(btnLowCn);

    auto *btnSigLoss = new QPushButton(QStringLiteral("Sinyali Kopar"), tableGroup);
    btnSigLoss->setStyleSheet("QPushButton { background-color: #991B1B; border-color: #EF4444; color: white; }");
    connect(btnSigLoss, &QPushButton::clicked, this, &SimulatorWindow::onInjectSignalLoss);
    faultBar->addWidget(btnSigLoss);

    auto *btnVidLoss = new QPushButton(QStringLiteral("Videoyu Kes"), tableGroup);
    btnVidLoss->setStyleSheet("QPushButton { background-color: #9A3412; border-color: #FB923C; color: white; }");
    connect(btnVidLoss, &QPushButton::clicked, this, &SimulatorWindow::onInjectVideoLoss);
    faultBar->addWidget(btnVidLoss);

    auto *btnRestore = new QPushButton(QStringLiteral("Normale Çevir"), tableGroup);
    connect(btnRestore, &QPushButton::clicked, this, &SimulatorWindow::onRestoreNormalPoint);
    faultBar->addWidget(btnRestore);

    auto *btnJump = new QPushButton(QStringLiteral("Bu Adımdan Gönder"), tableGroup);
    connect(btnJump, &QPushButton::clicked, this, &SimulatorWindow::onJumpToSelectedStep);
    faultBar->addWidget(btnJump);

    tableLay->addLayout(faultBar);

    m_table = new QTableWidget(0, 8, tableGroup);
    m_table->setHorizontalHeaderLabels({
        "#", "C/N (dB)", "Güç (dBm)", "Sinyal", "Video", "Ses", "BER", "Not / Durum"
    });
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    connect(m_table, &QTableWidget::cellChanged, this, &SimulatorWindow::onTableCellChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &SimulatorWindow::onTableSelectionChanged);
    tableLay->addWidget(m_table);

    // Right: Live status + TCP activity log
    auto *diagGroup = new QGroupBox(QStringLiteral("Anlık Gönderilen Veri & TCP Bağlantı Günlüğü"), splitter);
    auto *diagLay = new QVBoxLayout(diagGroup);

    auto *liveGrid = new QGridLayout();
    m_lblLiveStep  = new QLabel("Adım: #0", diagGroup);
    m_lblLiveCn    = new QLabel("C/N: 14.50 dB", diagGroup);
    m_lblLivePower = new QLabel("Güç: -42.00 dBm", diagGroup);
    m_lblLiveLock  = new QLabel("Kilit: Sinyal/Video/Ses OK", diagGroup);
    m_lblLastPeer  = new QLabel("Son İstemci: Bekleniyor | İstek: 0", diagGroup);
    m_lblTunedInfo = new QLabel("Tune: 11977 MHz | 27500 kS/s | H", diagGroup);

    m_lblLiveCn->setStyleSheet("color: #10B981; font-weight: bold;");
    m_lblLivePower->setStyleSheet("color: #38BDF8; font-weight: bold;");
    m_lblLastPeer->setStyleSheet("color: #FBBF24; font-weight: bold;");

    liveGrid->addWidget(m_lblLiveStep,  0, 0);
    liveGrid->addWidget(m_lblLiveCn,    0, 1);
    liveGrid->addWidget(m_lblLivePower, 0, 2);
    liveGrid->addWidget(m_lblLiveLock,  1, 0, 1, 2);
    liveGrid->addWidget(m_lblTunedInfo, 1, 2);
    liveGrid->addWidget(m_lblLastPeer,  2, 0, 1, 3);
    diagLay->addLayout(liveGrid);

    m_txtLog = new QTextEdit(diagGroup);
    m_txtLog->setReadOnly(true);
    diagLay->addWidget(m_txtLog, 1);

    splitter->addWidget(tableGroup);
    splitter->addWidget(diagGroup);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    rootLay->addWidget(splitter, 2);
    setCentralWidget(central);
}

void SimulatorWindow::populateBindAddresses() {
    m_comboBindIp->blockSignals(true);
    m_comboBindIp->clear();
    m_comboBindIp->addItem(QStringLiteral("0.0.0.0 (Tüm Ağ Arayüzleri / LAN)"), QStringLiteral("0.0.0.0"));

    QString primaryCandidate;
    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addrs) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            const QString ipStr = addr.toString();
            m_comboBindIp->addItem(QStringLiteral("%1 (Yerel Ağ IP)").arg(ipStr), ipStr);
            if (primaryCandidate.isEmpty() && !ipStr.startsWith("169.254.")) {
                primaryCandidate = ipStr;
            }
        }
    }
    m_cachedPrimaryLanIp = primaryCandidate.isEmpty() ? QStringLiteral("127.0.0.1") : primaryCandidate;
    m_comboBindIp->addItem(QStringLiteral("127.0.0.1 (Sadece Bu PC)"), QStringLiteral("127.0.0.1"));
    m_comboBindIp->blockSignals(false);
}

QString SimulatorWindow::detectPrimaryLanIp() const {
    return m_cachedPrimaryLanIp;
}

void SimulatorWindow::refreshEndpointHint() {
    const QString selectedIp = m_comboBindIp->currentData().toString();
    const QString connectIp = (selectedIp == "0.0.0.0" || selectedIp.isEmpty())
                                  ? detectPrimaryLanIp()
                                  : selectedIp;
    const int port = m_spinPort->value();
    m_lblEndpoint->setText(
        QStringLiteral("Diğer PC'deki Ana Uygulamaya Girilecek Adres ->  IP: %1   Port: %2")
            .arg(connectIp)
            .arg(port));
}

void SimulatorWindow::onToggleServer() {
    if (m_server->isListening()) {
        m_server->stop();
        m_btnStartStop->setText(QStringLiteral("TCP Yayını Başlat"));
        m_btnStartStop->setStyleSheet("QPushButton { background-color: #059669; border-color: #10B981; color: white; }");
        m_lblTcpStatus->setText(QStringLiteral("DURDURULDU"));
        m_lblTcpStatus->setStyleSheet("background-color: #334155; color: #CBD5E1; border-radius: 4px; padding: 4px; font-weight: bold;");
        m_comboBindIp->setEnabled(true);
        m_spinPort->setEnabled(true);
        return;
    }

    const QString bindIp = m_comboBindIp->currentData().toString();
    const quint16 port = static_cast<quint16>(m_spinPort->value());

    if (m_server->start(bindIp, port)) {
        m_server->resetRequestCount();
        syncLiveSampleToServer();
        m_btnStartStop->setText(QStringLiteral("TCP Yayını Durdur"));
        m_btnStartStop->setStyleSheet("QPushButton { background-color: #DC2626; border-color: #EF4444; color: white; }");
        m_lblTcpStatus->setText(QStringLiteral("YAYINDA"));
        m_lblTcpStatus->setStyleSheet("background-color: #065F46; color: #34D399; border-radius: 4px; padding: 4px; font-weight: bold;");
        m_comboBindIp->setEnabled(false);
        m_spinPort->setEnabled(false);
    } else {
        QMessageBox::warning(
            this,
            QStringLiteral("TCP Port Hatası"),
            QStringLiteral("Port %1 dinlenemedi:\n%2\n\nİpucu: Port 80 başka bir servis (IIS/HTTP.sys) tarafından kullanılıyorsa 8080 veya 9200 portunu seçebilirsiniz.")
                .arg(port)
                .arg(m_server->lastError()));
    }
}

void SimulatorWindow::onPlayPause() {
    m_isPlaying = m_btnPlayPause->isChecked();
    if (m_isPlaying) {
        m_btnPlayPause->setText(QStringLiteral("Akışı Duraklat"));
        m_playTimer->start();
    } else {
        m_btnPlayPause->setText(QStringLiteral("Akışı Başlat"));
        m_playTimer->stop();
    }
}

void SimulatorWindow::onRewindPlayback() {
    setActivePlayIndex(0);
}

void SimulatorWindow::onPlaybackTick() {
    if (m_points.isEmpty()) return;
    int nextIdx = m_playIndex + 1;
    if (nextIdx >= m_points.size()) {
        if (m_chkLoop->isChecked()) {
            nextIdx = 0;
        } else {
            m_isPlaying = false;
            m_btnPlayPause->setChecked(false);
            m_btnPlayPause->setText(QStringLiteral("Akışı Başlat"));
            m_playTimer->stop();
            return;
        }
    }
    setActivePlayIndex(nextIdx);
}

void SimulatorWindow::setActivePlayIndex(int index) {
    if (m_points.isEmpty()) return;
    m_playIndex = qBound(0, index, m_points.size() - 1);
    m_curveWidget->setPlayheadIndex(m_playIndex);
    syncLiveSampleToServer();
}

void SimulatorWindow::syncLiveSampleToServer() {
    if (m_points.isEmpty() || m_playIndex < 0 || m_playIndex >= m_points.size()) return;
    const SimTelemetryPoint &pt = m_points.at(m_playIndex);
    m_server->setLiveSample(pt);

    m_lblLiveStep->setText(QStringLiteral("Adım: #%1 / %2").arg(m_playIndex).arg(m_points.size() - 1));
    m_lblLiveCn->setText(QStringLiteral("C/N: %1 dB").arg(pt.cnDb, 0, 'f', 2));
    m_lblLivePower->setText(QStringLiteral("Güç: %1 dBm").arg(pt.powerDbm, 0, 'f', 2));

    QString lockStr = QStringLiteral("S:%1 V:%2 A:%3")
                          .arg(pt.signalLock ? "OK" : "YOK")
                          .arg(pt.videoLock ? "OK" : "YOK")
                          .arg(pt.audioLock ? "OK" : "YOK");
    m_lblLiveLock->setText(QStringLiteral("Kilit: %1 (%2)").arg(lockStr, pt.note));
    m_lblLiveLock->setStyleSheet(pt.isFaulty() ? "color: #F87171; font-weight: bold;"
                                               : "color: #34D399; font-weight: bold;");
}

void SimulatorWindow::rebuildTable() {
    const QSignalBlocker blocker(m_table);
    m_syncingTable = true;
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(m_points.size());
    for (int r = 0; r < m_points.size(); ++r) {
        for (int c = 0; c < 8; ++c) {
            if (!m_table->item(r, c)) {
                auto *item = new QTableWidgetItem();
                if (c == 0) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                    item->setTextAlignment(Qt::AlignCenter);
                } else if (c >= 3 && c <= 5) {
                    item->setFlags((item->flags() & ~Qt::ItemIsEditable) | Qt::ItemIsUserCheckable);
                }
                m_table->setItem(r, c, item);
            }
        }
        updateTableRow(r);
    }
    m_table->setUpdatesEnabled(true);
    m_syncingTable = false;
}

void SimulatorWindow::updateTableRow(int row) {
    if (row < 0 || row >= m_points.size() || !m_table) return;
    for (int c = 0; c < 8; ++c) {
        if (!m_table->item(row, c)) return;
    }

    const QSignalBlocker blocker(m_table);
    const bool prevSync = m_syncingTable;
    m_syncingTable = true;

    const SimTelemetryPoint &pt = m_points.at(row);
    m_table->item(row, 0)->setText(QString::number(row));
    m_table->item(row, 1)->setText(QString::number(pt.cnDb, 'f', 2));
    m_table->item(row, 2)->setText(QString::number(pt.powerDbm, 'f', 2));

    m_table->item(row, 3)->setCheckState(pt.signalLock ? Qt::Checked : Qt::Unchecked);
    m_table->item(row, 4)->setCheckState(pt.videoLock ? Qt::Checked : Qt::Unchecked);
    m_table->item(row, 5)->setCheckState(pt.audioLock ? Qt::Checked : Qt::Unchecked);
    m_table->item(row, 6)->setText(pt.ber);
    m_table->item(row, 7)->setText(pt.note);

    const QColor rowFg = pt.isFaulty() ? QColor("#FCA5A5") : QColor("#E2E8F0");
    for (int c = 0; c < 8; ++c) {
        m_table->item(row, c)->setForeground(rowFg);
    }

    m_syncingTable = prevSync;
}

void SimulatorWindow::onCurvePointEdited(int index, double, double) {
    updateTableRow(index);
    if (index == m_playIndex) {
        syncLiveSampleToServer();
    }
}

void SimulatorWindow::onGraphStepSelected(int index) {
    if (index < 0 || index >= m_table->rowCount()) return;
    const QSignalBlocker blocker(m_table);
    m_syncingTable = true;
    m_table->selectRow(index);
    m_syncingTable = false;
}

void SimulatorWindow::onTableSelectionChanged() {
    if (m_syncingTable) return;
    const int row = m_table->currentRow();
    if (row >= 0 && row < m_points.size()) {
        m_curveWidget->setSelectedIndex(row);
    }
}

void SimulatorWindow::onTableCellChanged(int row, int col) {
    if (m_syncingTable || row < 0 || row >= m_points.size()) return;
    SimTelemetryPoint &pt = m_points[row];
    QTableWidgetItem *item = m_table->item(row, col);
    if (!item) return;

    switch (col) {
    case 1:
        pt.cnDb = qBound(0.0, item->text().toDouble(), 30.0);
        break;
    case 2:
        pt.powerDbm = qBound(-100.0, item->text().toDouble(), 0.0);
        break;
    case 3:
        pt.signalLock = (item->checkState() == Qt::Checked);
        break;
    case 4:
        pt.videoLock = (item->checkState() == Qt::Checked);
        break;
    case 5:
        pt.audioLock = (item->checkState() == Qt::Checked);
        break;
    case 6:
        pt.ber = item->text().trimmed();
        break;
    case 7:
        pt.note = item->text().trimmed();
        break;
    default:
        break;
    }

    updateTableRow(row);
    m_curveWidget->refresh();
    if (row == m_playIndex) {
        syncLiveSampleToServer();
    }
}

void SimulatorWindow::onApplyPreset() {
    const auto preset = static_cast<SimCurveModel::PresetType>(m_comboPreset->currentData().toInt());
    const int count = qMax(10, m_points.size());
    m_points = SimCurveModel::createPreset(preset, count);
    m_curveWidget->setCurveData(&m_points);
    rebuildTable();
    setActivePlayIndex(0);
    appendLog(QStringLiteral("Şablon uygulandı: %1 (%2 adım)").arg(m_comboPreset->currentText()).arg(count));
}

void SimulatorWindow::onAddStep() {
    if (m_points.size() >= 200) return;
    SimTelemetryPoint p = m_points.isEmpty() ? SimTelemetryPoint{} : m_points.last();
    m_points.append(p);
    m_curveWidget->setCurveData(&m_points);
    rebuildTable();
    syncLiveSampleToServer();
}

void SimulatorWindow::onRemoveLastStep() {
    if (m_points.size() <= 5) return;
    m_points.removeLast();
    if (m_playIndex >= m_points.size()) {
        m_playIndex = m_points.size() - 1;
    }
    m_curveWidget->setCurveData(&m_points);
    rebuildTable();
    setActivePlayIndex(m_playIndex);
}

void SimulatorWindow::onInjectLowCnFault() {
    const int row = qMax(0, m_table->currentRow());
    if (row >= m_points.size()) return;
    SimTelemetryPoint &pt = m_points[row];
    pt.cnDb = 7.20;
    pt.powerDbm = -54.0;
    pt.signalLock = true;
    pt.videoLock = false;
    pt.audioLock = false;
    pt.ber = "4.2E-3";
    pt.note = QStringLiteral("Düşük C/N Hatası");
    updateTableRow(row);
    m_curveWidget->refresh();
    syncLiveSampleToServer();
}

void SimulatorWindow::onInjectSignalLoss() {
    const int row = qMax(0, m_table->currentRow());
    if (row >= m_points.size()) return;
    SimTelemetryPoint &pt = m_points[row];
    pt.cnDb = 0.0;
    pt.powerDbm = -85.0;
    pt.signalLock = false;
    pt.videoLock = false;
    pt.audioLock = false;
    pt.ber = "1.0E-1";
    pt.note = QStringLiteral("Sinyal Kopması");
    updateTableRow(row);
    m_curveWidget->refresh();
    syncLiveSampleToServer();
}

void SimulatorWindow::onInjectVideoLoss() {
    const int row = qMax(0, m_table->currentRow());
    if (row >= m_points.size()) return;
    SimTelemetryPoint &pt = m_points[row];
    pt.signalLock = true;
    pt.videoLock = false;
    pt.audioLock = false;
    pt.ber = "8.5E-4";
    pt.note = QStringLiteral("Video/Ses Yok");
    updateTableRow(row);
    m_curveWidget->refresh();
    syncLiveSampleToServer();
}

void SimulatorWindow::onRestoreNormalPoint() {
    const int row = qMax(0, m_table->currentRow());
    if (row >= m_points.size()) return;
    SimTelemetryPoint &pt = m_points[row];
    pt.cnDb = 14.50;
    pt.powerDbm = -42.0;
    pt.signalLock = true;
    pt.videoLock = true;
    pt.audioLock = true;
    pt.ber = "1.0E-7";
    pt.note = QStringLiteral("Normal");
    updateTableRow(row);
    m_curveWidget->refresh();
    syncLiveSampleToServer();
}

void SimulatorWindow::onJumpToSelectedStep() {
    const int row = m_table->currentRow();
    if (row >= 0 && row < m_points.size()) {
        setActivePlayIndex(row);
        appendLog(QStringLiteral("Yayın konumu Adım #%1 noktasına alındı.").arg(row));
    }
}

void SimulatorWindow::onImportCurve() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("RF Grafik Profilini İçe Aktar"),
        QString(),
        QStringLiteral("Desteklenen Dosyalar (*.json *.csv);;JSON Profili (*.json);;CSV Tablosu (*.csv)"));

    if (path.isEmpty()) return;

    QVector<SimTelemetryPoint> loaded;
    QString err;
    bool ok = false;

    if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) == 0) {
        ok = SimCurveModel::importCsv(path, loaded, &err);
    } else {
        QString curveName;
        int intervalMs = m_spinInterval->value();
        ok = SimCurveModel::importJson(path, loaded, &curveName, &intervalMs, &err);
        if (ok) {
            m_spinInterval->setValue(intervalMs);
        }
    }

    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("İçe Aktarma Hatası"), err);
        return;
    }

    m_points = loaded;
    m_curveWidget->setCurveData(&m_points);
    rebuildTable();
    setActivePlayIndex(0);
    appendLog(QStringLiteral("Grafik içe aktarıldı: %1 (%2 adım)").arg(QFileInfo(path).fileName()).arg(m_points.size()));
}

void SimulatorWindow::onExportCurve() {
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("RF Grafik Profilini Dışa Aktar"),
        QStringLiteral("drx_test_curve.json"),
        QStringLiteral("JSON Profili (*.json);;CSV Tablosu (*.csv)"),
        &selectedFilter);

    if (path.isEmpty()) return;

    QString err;
    bool ok = false;
    if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) == 0 ||
        selectedFilter.contains("CSV", Qt::CaseInsensitive)) {
        ok = SimCurveModel::exportCsv(path, m_points, &err);
    } else {
        ok = SimCurveModel::exportJson(path, m_points, m_comboPreset->currentText(), m_spinInterval->value(), &err);
    }

    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("Dışa Aktarma Hatası"), err);
        return;
    }

    appendLog(QStringLiteral("Grafik dışa aktarıldı: %1").arg(QFileInfo(path).fileName()));
}

void SimulatorWindow::onRequestHandled(quint64 totalCount, const QString &peerIp, const QString &actionTag) {
    m_lblLastPeer->setText(
        QStringLiteral("Son İstemci: %1 (%2)  |  Toplam İstek: %3")
            .arg(peerIp, actionTag)
            .arg(totalCount));
}

void SimulatorWindow::onServerTuned(int freqMhz, int symbolRate, const QString &pol) {
    m_lblTunedInfo->setText(
        QStringLiteral("Tune: %1 MHz | %2 kS/s | %3")
            .arg(freqMhz).arg(symbolRate).arg(pol));
}

void SimulatorWindow::appendLog(const QString &msg) {
    const QString line = QString("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), msg);
    m_txtLog->append(line);
}
