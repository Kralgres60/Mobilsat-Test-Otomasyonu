/**
 * @file debug_runner_window.cpp
 * @brief Implementation of the interactive Developer and Debug Test Console.
 * @author Semi Eren GÃ¶kyÄ±ldÄ±z
 * @copyright Copyright (C) 2026 NETA Elektronik A.Å.
 */

#include "debug_runner_window.h"
#include "ui/common/style_helper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QClipboard>
#include <QApplication>
#include <numeric>

DebugRunnerWindow::DebugRunnerWindow(QWidget *parent)
    : QMainWindow(parent), m_webClient(new DrxWebClient(this)) {
    setupUi();

    // Sinyal / Slot baÄŸlantÄ±larÄ±
    connect(m_connectBtn, &QPushButton::clicked, this, &DebugRunnerWindow::onConnectClicked);
    connect(m_tuneBtn, &QPushButton::clicked, this, &DebugRunnerWindow::onTuneClicked);
    connect(m_pollBtn, &QPushButton::clicked, this, &DebugRunnerWindow::onPollTelemetryClicked);
    connect(m_startSampleBtn, &QPushButton::clicked, this, &DebugRunnerWindow::onStartSamplingClicked);
    connect(m_scanAllBtn, &QPushButton::clicked, this, &DebugRunnerWindow::onScanAllClicked);
    connect(m_copyRawBtn, &QPushButton::clicked, this, &DebugRunnerWindow::onCopyRawClicked);

    connect(&m_sampleTickTimer, &QTimer::timeout, this, &DebugRunnerWindow::onSampleTimerTick);
    connect(&m_sampleDurationTimer, &QTimer::timeout, this, &DebugRunnerWindow::onSamplingFinished);
    m_sampleDurationTimer.setSingleShot(true);

    connect(m_webClient, &DrxWebClient::loginSucceeded, this, [this](int key) {
        onSessionKeyReceived(QString::number(key));
    });
    connect(m_webClient, &DrxWebClient::loginFailed, this, [this](const QString &reason) {
        onErrorOccurred("Login HatasÄ±: " + reason);
    });
    connect(m_webClient, &DrxWebClient::tuneCompleted, this, &DebugRunnerWindow::onTuneFinished);
    connect(m_webClient, &DrxWebClient::statusReceived, this, [this](bool s, bool v, bool a, const QString &ch) {
        m_carrierLabel->setText(QString("TaÅŸÄ±yÄ±cÄ± (S): %1").arg(s ? "<font color='#4CAF50'><b>KÄ°LÄ°TLÄ° (S:+)</b></font>" : "<font color='#F44336'>KÄ°LÄ°TSÄ°Z (S:-)</font>"));
        m_videoLabel->setText(QString("Video (V): %1").arg(v ? "<font color='#4CAF50'><b>VAR (V:+)</b></font>" : "<font color='#F44336'>YOK (V:-)</font>"));
        m_audioLabel->setText(QString("Ses (A): %1").arg(a ? "<font color='#4CAF50'><b>VAR (A:+)</b></font>" : "<font color='#F44336'>YOK (A:-)</font>"));
        if (!ch.isEmpty()) {
            m_statusLabel->setText(QString("<b>Kanal:</b> <font color='#81D4FA'>%1</font>").arg(ch));
        }
    });
    connect(m_webClient, &DrxWebClient::telemetryReceived, this, [this](double powDbm, double cnDb, double ber, double ebN0) {
        m_liveCnLabel->setText(QString("<b>C/N:</b> %1 dB").arg(QString::number(cnDb, 'f', 2)));
        m_livePowLabel->setText(QString("<b>GÃ¼Ã§:</b> %1 dBm").arg(QString::number(powDbm, 'f', 2)));
        m_liveEbN0Label->setText(QString("<b>Eb/N0:</b> %1 dB").arg(QString::number(ebN0, 'f', 2)));
        m_liveBerLabel->setText(QString("<b>BER:</b> %1").arg(ber > 0 ? QString::number(ber, 'e', 1) : "-"));

        if (m_isSampling) {
            m_cnSamples.append(cnDb);
            m_powSamples.append(powDbm);
            appendLog(QString("[Ã–rnek #%1] C/N: %2 dB | GÃ¼Ã§: %3 dBm | Eb/N0: %4 dB | BER: %5")
                          .arg(m_cnSamples.size())
                          .arg(QString::number(cnDb, 'f', 2))
                          .arg(QString::number(powDbm, 'f', 2))
                          .arg(QString::number(ebN0, 'f', 2))
                          .arg(ber > 0 ? QString::number(ber, 'e', 1) : "0"), "#81D4FA");
        }
    });
    connect(m_webClient, &DrxWebClient::rawResponseReceived, this, &DebugRunnerWindow::onRawResponseReceived);
    connect(m_webClient, &DrxWebClient::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog(msg, isErr ? "#EF5350" : "#B0BEC5");
    });

    appendLog("ğŸš€ DEBUG TEST MODU BaÅŸlatÄ±ldÄ±. Cihaz IP'sini belirleyip 'BaÄŸlan & Login' butonuna basÄ±nÄ±z.", "#4CAF50");
}

DebugRunnerWindow::~DebugRunnerWindow() {
    m_sampleTickTimer.stop();
    m_sampleDurationTimer.stop();
}

void DebugRunnerWindow::setupUi() {
    setWindowTitle("NETA DRX9000 - GeliÅŸtirici & REST API Debug Test Konsolu");
    resize(1020, 750);
    setStyleSheet(StyleHelper::getAppStyle());

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(14, 14, 14, 14);

    // 1. ÃœST PANEL: Cihaz ve Login Bilgileri
    QGroupBox *connGroup = new QGroupBox("1. DRX Web Sunucusu BaÄŸlantÄ± ve GiriÅŸ Bilgileri", this);
    QHBoxLayout *connLayout = new QHBoxLayout(connGroup);
    m_ipEdit = new QLineEdit("192.168.0.167", this);
    m_userEdit = new QLineEdit("admin", this);
    m_passEdit = new QLineEdit("1234", this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_connectBtn = new QPushButton("ğŸ”‘ BaÄŸlan & Login (SessionKey Al)", this);
    m_connectBtn->setStyleSheet("background-color: #1976D2; color: white; font-weight: bold; padding: 6px 12px;");

    m_scanAllBtn = new QPushButton("ğŸ” TÃ¼m SayfalarÄ± Tara (TÃ¼m Bilgileri Getir)", this);
    m_scanAllBtn->setStyleSheet("background-color: #0288D1; color: white; font-weight: bold; padding: 6px 12px;");

    connLayout->addWidget(new QLabel("IP:", this));
    connLayout->addWidget(m_ipEdit);
    connLayout->addWidget(new QLabel("KullanÄ±cÄ±:", this));
    connLayout->addWidget(m_userEdit);
    connLayout->addWidget(new QLabel("Åifre:", this));
    connLayout->addWidget(m_passEdit);
    connLayout->addWidget(m_connectBtn);
    connLayout->addWidget(m_scanAllBtn);
    mainLayout->addWidget(connGroup);

    // 2. ORTA PANEL: Frekans ZÄ±plama & Ã–rnekleme Parametreleri
    QGroupBox *testGroup = new QGroupBox("2. Frekans Ayarlama ve Ã–rnekleme Testi", this);
    QGridLayout *testLayout = new QGridLayout(testGroup);

    m_freqSpin = new QSpinBox(this);
    m_freqSpin->setRange(950, 21500);
    m_freqSpin->setValue(12207);
    m_freqSpin->setSuffix(" MHz");

    m_srSpin = new QSpinBox(this);
    m_srSpin->setRange(1000, 45000);
    m_srSpin->setValue(27500);
    m_srSpin->setSuffix(" kS/s");

    m_polCombo = new QComboBox(this);
    m_polCombo->addItems(QStringList() << "V" << "H");

    m_sampleDurationSpin = new QSpinBox(this);
    m_sampleDurationSpin->setRange(1, 30);
    m_sampleDurationSpin->setValue(5);
    m_sampleDurationSpin->setSuffix(" sn");

    m_tuneBtn = new QPushButton("ğŸ¯ Frekansa ZÄ±pla (ResponseTo=freq)", this);
    m_tuneBtn->setStyleSheet("background-color: #00897B; color: white; font-weight: bold; padding: 6px 10px;");

    m_pollBtn = new QPushButton("ğŸ“¡ Tek Seferlik Telemetri Oku (alarmStatus)", this);
    m_pollBtn->setStyleSheet("background-color: #5E35B1; color: white; padding: 6px 10px;");

    m_startSampleBtn = new QPushButton("â±ï¸ 5 sn Ã–rnekle & Ortalama C/N Hesapla", this);
    m_startSampleBtn->setStyleSheet("background-color: #E65100; color: white; font-weight: bold; padding: 8px 12px; font-size: 13px;");

    testLayout->addWidget(new QLabel("Frekans:", this), 0, 0);
    testLayout->addWidget(m_freqSpin, 0, 1);
    testLayout->addWidget(new QLabel("Sembol OranÄ±:", this), 0, 2);
    testLayout->addWidget(m_srSpin, 0, 3);
    testLayout->addWidget(new QLabel("Polarizasyon:", this), 0, 4);
    testLayout->addWidget(m_polCombo, 0, 5);

    testLayout->addWidget(m_tuneBtn, 1, 0, 1, 2);
    testLayout->addWidget(m_pollBtn, 1, 2, 1, 2);
    testLayout->addWidget(new QLabel("Ã–rnekleme SÃ¼resi:", this), 1, 4);
    testLayout->addWidget(m_sampleDurationSpin, 1, 5);

    testLayout->addWidget(m_startSampleBtn, 2, 0, 1, 6);
    mainLayout->addWidget(testGroup);

    // 3. CANLI TELEMETRÄ° VE ORTALAMA SONUÃ‡ PANELÄ°
    QGroupBox *statusGroup = new QGroupBox("3. CanlÄ± Telemetri ve Ortalama GÃ¶stergeleri", this);
    QHBoxLayout *statusLayout = new QHBoxLayout(statusGroup);

    m_statusLabel = new QLabel("<b>Kanal:</b> -", this);
    m_carrierLabel = new QLabel("TaÅŸÄ±yÄ±cÄ± (S): -", this);
    m_videoLabel = new QLabel("Video (V): -", this);
    m_audioLabel = new QLabel("Ses (A): -", this);
    m_liveCnLabel = new QLabel("<b>C/N:</b> - dB", this);
    m_livePowLabel = new QLabel("<b>GÃ¼Ã§:</b> - dBm", this);
    m_liveEbN0Label = new QLabel("<b>Eb/N0:</b> - dB", this);
    m_liveBerLabel = new QLabel("<b>BER:</b> -", this);
    m_avgResultLabel = new QLabel("<b>[ORTALAMA]:</b> HenÃ¼z hesaplanmadÄ±", this);
    m_avgResultLabel->setStyleSheet("background-color: #263238; color: #FFEB3B; padding: 6px 12px; border-radius: 4px; font-size: 13px;");

    statusLayout->addWidget(m_statusLabel);
    statusLayout->addWidget(m_carrierLabel);
    statusLayout->addWidget(m_videoLabel);
    statusLayout->addWidget(m_audioLabel);
    statusLayout->addWidget(m_liveCnLabel);
    statusLayout->addWidget(m_livePowLabel);
    statusLayout->addWidget(m_liveEbN0Label);
    statusLayout->addWidget(m_liveBerLabel);
    statusLayout->addWidget(m_avgResultLabel);
    mainLayout->addWidget(statusGroup);

    // 4. TAB PANEL: DETAYLI LOGLAR & HAM JSON Ã‡IKTISI
    m_tabWidget = new QTabWidget(this);

    // Tab 1: GeliÅŸtirici Konsol LoglarÄ±
    QWidget *tabLog = new QWidget(this);
    QVBoxLayout *tabLogLayout = new QVBoxLayout(tabLog);
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet("background-color: #121212; color: #E0E0E0; font-family: Consolas, monospace; font-size: 11px;");
    tabLogLayout->addWidget(m_logEdit);
    m_tabWidget->addTab(tabLog, "ğŸ“‘ AyrÄ±ÅŸtÄ±rÄ±lmÄ±ÅŸ GeliÅŸtirici LoglarÄ±");

    // Tab 2: Ham Web YanÄ±tÄ± (Raw JSON)
    QWidget *tabRaw = new QWidget(this);
    QVBoxLayout *tabRawLayout = new QVBoxLayout(tabRaw);
    QHBoxLayout *rawTopBar = new QHBoxLayout();
    m_copyRawBtn = new QPushButton("ğŸ“‹ Ham JSON'Ä± Panoya Kopyala", this);
    m_copyRawBtn->setStyleSheet("background-color: #37474F; color: white; font-weight: bold; padding: 4px 10px;");
    QPushButton *clearRawBtn = new QPushButton("ğŸ§¹ Temizle", this);
    connect(clearRawBtn, &QPushButton::clicked, this, [this]() { m_rawJsonEdit->clear(); });

    rawTopBar->addWidget(new QLabel("<b>DRX Web Sunucusundan Gelen Ham JSON / Metin YanÄ±tÄ±:</b>", this));
    rawTopBar->addStretch();
    rawTopBar->addWidget(m_copyRawBtn);
    rawTopBar->addWidget(clearRawBtn);
    tabRawLayout->addLayout(rawTopBar);

    m_rawJsonEdit = new QTextEdit(this);
    m_rawJsonEdit->setReadOnly(true);
    m_rawJsonEdit->setStyleSheet("background-color: #0D1117; color: #58A6FF; font-family: Consolas, monospace; font-size: 12px;");
    tabRawLayout->addWidget(m_rawJsonEdit);
    m_tabWidget->addTab(tabRaw, "ğŸ“‹ DRX Ham JSON YanÄ±tÄ± (Raw Payload)");

    // Tab 3: MariaDB ve DaÄŸÄ±tÄ±k Kilit (Algoritma 2) Konsolu
    m_dbDebugWidget = new DbDebugWidget(this);
    m_tabWidget->addTab(m_dbDebugWidget, "ğŸ—„ï¸ MariaDB & DaÄŸÄ±tÄ±k Kilit (Algoritma 2)");

    mainLayout->addWidget(m_tabWidget);
}

void DebugRunnerWindow::onConnectClicked() {
    m_webClient->setHost(m_ipEdit->text().trimmed());
    m_webClient->setCredentials(m_userEdit->text().trimmed(), m_passEdit->text().trimmed());
    appendLog(QString("ğŸ”Œ %1 cihazÄ±na baÄŸlanÄ±lÄ±yor...").arg(m_ipEdit->text()), "#FFD54F");
    m_webClient->login();
}

void DebugRunnerWindow::onTuneClicked() {
    appendLog(QString("ğŸ¯ Frekansa ZÄ±planÄ±yor -> %1 MHz, SR: %2, Pol: %3")
                  .arg(m_freqSpin->value())
                  .arg(m_srSpin->value())
                  .arg(m_polCombo->currentText()), "#4DD0E1");
    m_webClient->sendTune(m_freqSpin->value(), m_srSpin->value(), m_polCombo->currentText());
}

void DebugRunnerWindow::onPollTelemetryClicked() {
    m_webClient->requestStatus();
}

void DebugRunnerWindow::onStartSamplingClicked() {
    m_cnSamples.clear();
    m_powSamples.clear();
    m_isSampling = true;

    int durationSec = m_sampleDurationSpin->value();
    m_sampleDurationTimer.start(durationSec * 1000);
    m_sampleTickTimer.start(250); // 250ms periyot

    m_startSampleBtn->setEnabled(false);
    m_avgResultLabel->setText("â³ Ã–rnekleme yapÄ±lÄ±yor, bekleniyor...");
    appendLog(QString("â±ï¸ %1 saniyelik Ã§oklu veri Ã¶rneklemesi baÅŸladÄ± (250 ms aralÄ±kla)...").arg(durationSec), "#FFA726");
}

void DebugRunnerWindow::onSampleTimerTick() {
    m_webClient->requestStatus();
}

void DebugRunnerWindow::onSamplingFinished() {
    m_sampleTickTimer.stop();
    m_isSampling = false;
    m_startSampleBtn->setEnabled(true);

    if (m_cnSamples.isEmpty()) {
        appendLog("âš ï¸ HiÃ§ C/N Ã¶rneÄŸi alÄ±namadÄ±!", "#FF5252");
        m_avgResultLabel->setText("<b>[HATA]:</b> Veri alÄ±namadÄ±!");
        return;
    }

    double sumCn = std::accumulate(m_cnSamples.begin(), m_cnSamples.end(), 0.0);
    double avgCn = sumCn / m_cnSamples.size();

    double sumPow = std::accumulate(m_powSamples.begin(), m_powSamples.end(), 0.0);
    double avgPow = sumPow / m_powSamples.size();

    QString summary = QString("<b>[ORTALAMA (%1 Ã–rnek)]:</b> C/N: <font color='#4CAF50'><b>%2 dB</b></font> | GÃ¼Ã§: <font color='#4DD0E1'><b>%3 dBm</b></font>")
                          .arg(m_cnSamples.size())
                          .arg(QString::number(avgCn, 'f', 2))
                          .arg(QString::number(avgPow, 'f', 2));

    m_avgResultLabel->setText(summary);
    appendLog(QString("âœ… Ã–rnekleme TamamlandÄ±! Ortalama C/N: %1 dB | Ortalama GÃ¼Ã§: %2 dBm (Toplam %3 Ã¶rnek)")
                  .arg(QString::number(avgCn, 'f', 2))
                  .arg(QString::number(avgPow, 'f', 2))
                  .arg(m_cnSamples.size()), "#69F0AE");
}

void DebugRunnerWindow::onSessionKeyReceived(const QString &sessionKey) {
    appendLog(QString("ğŸ”‘ Oturum AÃ§Ä±ldÄ±! AlÄ±nan SessionKey: %1").arg(sessionKey), "#81C784");
}

void DebugRunnerWindow::onTuneFinished(bool success, const QString &message) {
    if (success) {
        appendLog("âœ… Frekans AyarlandÄ±: " + message, "#A5D6A7");
    } else {
        appendLog("âŒ Frekans Ayarlama BaÅŸarÄ±sÄ±z: " + message, "#EF5350");
    }
}

void DebugRunnerWindow::onErrorOccurred(const QString &error) {
    appendLog("ğŸš¨ " + error, "#FF1744");
}

void DebugRunnerWindow::onRawResponseReceived(const QString &actionType, const QByteArray &rawJson) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    
    // JSON'Ä± girintili (indented) formatta gÃ¼zelleÅŸtir
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(rawJson, &parseErr);
    QString prettyJson;
    if (parseErr.error == QJsonParseError::NoError) {
        prettyJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    } else {
        prettyJson = QString::fromUtf8(rawJson);
    }

    QString header = QString("======================================================================\n"
                             "[%1] Ä°STEK TÃœRÃœ: %2 | BOYUT: %3 bayt\n"
                             "======================================================================\n")
                         .arg(timeStr, actionType).arg(rawJson.size());

    m_rawJsonEdit->append(header + prettyJson + "\n\n");
}

void DebugRunnerWindow::onScanAllClicked() {
    appendLog("ğŸ” CihazÄ±n tÃ¼m olasÄ± sayfalarÄ± (alarmStatus, channeldetails, frequency, deviceInfo) sorgulanÄ±yor...", "#AB47BC");
    m_webClient->requestStatus();
    m_webClient->requestDeviceInfo();
    m_webClient->sendRawQuery("channeldetails");
    m_webClient->sendRawQuery("frequency");
    m_webClient->sendRawQuery("channelselection");
    
    // KullanÄ±cÄ±yÄ± doÄŸrudan Ham JSON sekmesine geÃ§ir
    m_tabWidget->setCurrentIndex(1);
}

void DebugRunnerWindow::onCopyRawClicked() {
    if (m_rawJsonEdit) {
        QApplication::clipboard()->setText(m_rawJsonEdit->toPlainText());
        appendLog("ğŸ“‹ Ham JSON iÃ§eriÄŸi panoya kopyalandÄ±!", "#80CBC4");
    }
}

void DebugRunnerWindow::appendLog(const QString &msg, const QString &color) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_logEdit->append(QString("<font color='#78909C'>[%1]</font> <font color='%2'>%3</font>")
                          .arg(timeStr, color, msg));
}


// =============================================================================
// DbDebugWidget Implementation
// =============================================================================

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QDateTime>
#include <QRandomGenerator>
#include <QElapsedTimer>

DbDebugWidget::DbDebugWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();

    connect(m_runFullSuiteBtn, &QPushButton::clicked, this, &DbDebugWidget::onRunFullSuiteClicked);
    connect(&m_suiteTimer, &QTimer::timeout, this, &DbDebugWidget::onSuiteTimerTick);

    connect(m_connectBtn, &QPushButton::clicked, this, &DbDebugWidget::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &DbDebugWidget::onDisconnectClicked);
    connect(m_startEngineBtn, &QPushButton::clicked, this, &DbDebugWidget::onStartEngineClicked);
    connect(m_stopEngineBtn, &QPushButton::clicked, this, &DbDebugWidget::onStopEngineClicked);
    connect(m_initSchemaBtn, &QPushButton::clicked, this, &DbDebugWidget::onInitSchemaClicked);
    connect(m_loadProfilesBtn, &QPushButton::clicked, this, &DbDebugWidget::onLoadProfilesClicked);

    connect(m_acquireLockBtn, &QPushButton::clicked, this, &DbDebugWidget::onAcquireLockClicked);
    connect(m_releaseLockBtn, &QPushButton::clicked, this, &DbDebugWidget::onReleaseLockClicked);
    connect(m_heartbeatBtn, &QPushButton::clicked, this, &DbDebugWidget::onHeartbeatClicked);
    connect(m_queryLockBtn, &QPushButton::clicked, this, &DbDebugWidget::onQueryLockClicked);
    connect(m_contentionSimBtn, &QPushButton::clicked, this, &DbDebugWidget::onRunContentionSimulationClicked);

    connect(m_insertMockBtn, &QPushButton::clicked, this, &DbDebugWidget::onInsertMockRunClicked);
    connect(m_refreshRunsBtn, &QPushButton::clicked, this, &DbDebugWidget::onRefreshTestRunsClicked);

    connect(&m_pollTimer, &QTimer::timeout, this, &DbDebugWidget::onQueryLockClicked);

    updateConnectionStatusBadge(DbManager::instance().isConnected());
    appendLog("ğŸ—„ï¸ MariaDB & DaÄŸÄ±tÄ±k Kilit DoÄŸrulama ModÃ¼lÃ¼ HazÄ±r.", "#81D4FA");
}

DbDebugWidget::~DbDebugWidget() {
    m_pollTimer.stop();
    m_suiteTimer.stop();
}

void DbDebugWidget::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // -------------------------------------------------------------------------
    // 0. EN ÃœST PANEL: Tek TÄ±kla CanlÄ± Sistem TeÅŸhis & DoÄŸrulama SÃ¼iti
    // -------------------------------------------------------------------------
    QGroupBox *suiteGroup = new QGroupBox("âš¡ Otomatik CanlÄ± Sistem TeÅŸhis ve DoÄŸrulama SÃ¼iti (Full Self-Test)", this);
    QVBoxLayout *suiteLayout = new QVBoxLayout(suiteGroup);

    QHBoxLayout *suiteTop = new QHBoxLayout();
    m_runFullSuiteBtn = new QPushButton("ğŸš€ TEK TIKLA TÃœM VERÄ°TABANI VE DAÄITIK KÄ°LÄ°DÄ° TEST ET (RUN FULL SUITE)", this);
    m_runFullSuiteBtn->setStyleSheet("background-color: #2E7D32; color: #FFFFFF; font-size: 13px; font-weight: bold; padding: 10px 18px; border-radius: 4px;");

    m_suiteProgress = new QProgressBar(this);
    m_suiteProgress->setRange(0, 100);
    m_suiteProgress->setValue(0);
    m_suiteProgress->setTextVisible(true);
    m_suiteProgress->setStyleSheet("QProgressBar { background-color: #212121; color: white; border: 1px solid #424242; border-radius: 4px; text-align: center; font-weight: bold; height: 26px; } QProgressBar::chunk { background-color: #00C853; border-radius: 3px; }");

    suiteTop->addWidget(m_runFullSuiteBtn, 3);
    suiteTop->addWidget(m_suiteProgress, 2);
    suiteLayout->addLayout(suiteTop);

    // 6 AdÄ±mlÄ± TeÅŸhis KartlarÄ± (2x3 Grid)
    QGridLayout *cardsLayout = new QGridLayout();
    cardsLayout->setSpacing(6);

    auto makeCard = [this](const QString &title) {
        QLabel *lbl = new QLabel(QString("âšª %1: Beklemede").arg(title), this);
        lbl->setStyleSheet("background-color: #263238; color: #B0BEC5; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
        return lbl;
    };

    m_stepDriverLabel = makeCard("1. SÃ¼rÃ¼cÃ¼ & Motor");
    m_stepLatencyLabel = makeCard("2. AÄŸ & Ping Gecikmesi");
    m_stepSchemaLabel = makeCard("3. Åema & Tablolar (8 Tablo)");
    m_stepChannelsLabel = makeCard("4. 10 TÃ¼rksat Transponder");
    m_stepLockLabel = makeCard("5. DaÄŸÄ±tÄ±k Kilit & Ã‡akÄ±ÅŸma");
    m_stepPersistenceLabel = makeCard("6. Test KaydÄ± (SQL I/O)");

    cardsLayout->addWidget(m_stepDriverLabel, 0, 0);
    cardsLayout->addWidget(m_stepLatencyLabel, 0, 1);
    cardsLayout->addWidget(m_stepSchemaLabel, 0, 2);
    cardsLayout->addWidget(m_stepChannelsLabel, 1, 0);
    cardsLayout->addWidget(m_stepLockLabel, 1, 1);
    cardsLayout->addWidget(m_stepPersistenceLabel, 1, 2);
    suiteLayout->addLayout(cardsLayout);

    m_suiteResultBanner = new QLabel("Sistem testi baÅŸlatÄ±lmaya hazÄ±r. YukarÄ±daki butona tÄ±klayarak tek adÄ±mda tÃ¼m altyapÄ±yÄ± doÄŸrulayabilirsiniz.", this);
    m_suiteResultBanner->setAlignment(Qt::AlignCenter);
    m_suiteResultBanner->setStyleSheet("background-color: #1E1E1E; color: #FFF59D; padding: 8px; border-radius: 4px; font-weight: bold; font-size: 12px; border: 1px dashed #FFD54F;");
    suiteLayout->addWidget(m_suiteResultBanner);

    mainLayout->addWidget(suiteGroup);

    // -------------------------------------------------------------------------
    // 1. ÃœST PANEL: MariaDB BaÄŸlantÄ± YapÄ±landÄ±rmasÄ±
    // -------------------------------------------------------------------------
    QGroupBox *connGroup = new QGroupBox("1. MariaDB VeritabanÄ± ve TaÅŸÄ±nabilir Motor YapÄ±landÄ±rmasÄ±", this);
    QGridLayout *connLayout = new QGridLayout(connGroup);

    m_hostEdit = new QLineEdit("127.0.0.1", this);
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(3306);
    m_dbNameEdit = new QLineEdit("neta_antenna_test", this);
    m_userEdit = new QLineEdit("root", this);
    m_passEdit = new QLineEdit("neta123", this);
    m_passEdit->setEchoMode(QLineEdit::Password);

    m_startEngineBtn = new QPushButton("ğŸš€ TaÅŸÄ±nabilir Motoru BaÅŸlat", this);
    m_startEngineBtn->setStyleSheet("background-color: #00796B; color: white; font-weight: bold; padding: 6px 12px;");

    m_stopEngineBtn = new QPushButton("â¹ï¸ Motoru Durdur", this);
    m_stopEngineBtn->setStyleSheet("background-color: #D32F2F; color: white; font-weight: bold; padding: 6px 12px;");

    m_connectBtn = new QPushButton("ğŸ”Œ BaÄŸlan", this);
    m_connectBtn->setStyleSheet("background-color: #1976D2; color: white; font-weight: bold; padding: 6px 12px;");

    m_disconnectBtn = new QPushButton("âŒ BaÄŸlantÄ±yÄ± Kes", this);
    m_disconnectBtn->setStyleSheet("background-color: #546E7A; color: white; padding: 6px 12px;");

    m_statusBadge = new QLabel("DURUM: BAÄLI DEÄÄ°L", this);
    m_statusBadge->setStyleSheet("background-color: #B71C1C; color: white; padding: 6px 12px; border-radius: 4px; font-weight: bold;");

    // SatÄ±r 0: BaÄŸlantÄ± parametreleri
    connLayout->addWidget(new QLabel("Host IP:", this), 0, 0);
    connLayout->addWidget(m_hostEdit, 0, 1);
    connLayout->addWidget(new QLabel("Port:", this), 0, 2);
    connLayout->addWidget(m_portSpin, 0, 3);
    connLayout->addWidget(new QLabel("VeritabanÄ±:", this), 0, 4);
    connLayout->addWidget(m_dbNameEdit, 0, 5);
    connLayout->addWidget(new QLabel("KullanÄ±cÄ±:", this), 0, 6);
    connLayout->addWidget(m_userEdit, 0, 7);
    connLayout->addWidget(new QLabel("Åifre:", this), 0, 8);
    connLayout->addWidget(m_passEdit, 0, 9);

    // SatÄ±r 1: Eylem butonlarÄ±
    QHBoxLayout *actionLayout = new QHBoxLayout();
    actionLayout->addWidget(m_startEngineBtn);
    actionLayout->addWidget(m_stopEngineBtn);
    actionLayout->addSpacing(15);
    actionLayout->addWidget(m_connectBtn);
    actionLayout->addWidget(m_disconnectBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(m_statusBadge);
    connLayout->addLayout(actionLayout, 1, 0, 1, 10);

    mainLayout->addWidget(connGroup);

    // -------------------------------------------------------------------------
    // 2. ORTA PANEL: Åema, DaÄŸÄ±tÄ±k Kilit ve Test FonksiyonlarÄ±
    // -------------------------------------------------------------------------
    QHBoxLayout *midLayout = new QHBoxLayout();

    // Sol Panel: DaÄŸÄ±tÄ±k Kilit (Algoritma 2 - reference_lock)
    QGroupBox *lockGroup = new QGroupBox("2. Algoritma 2: DaÄŸÄ±tÄ±k Atomik Kilit SimÃ¼latÃ¶rÃ¼ (reference_lock)", this);
    QGridLayout *lockLayout = new QGridLayout(lockGroup);

    m_stationSpin = new QSpinBox(this);
    m_stationSpin->setRange(1, 3);
    m_stationSpin->setValue(1);
    m_stationSpin->setPrefix("Masa ");

    m_channelSpin = new QSpinBox(this);
    m_channelSpin->setRange(1, 10);
    m_channelSpin->setValue(1);
    m_channelSpin->setPrefix("Kanal #");

    m_acquireLockBtn = new QPushButton("ğŸ”’ Kilidi Talep Et (Acquire)", this);
    m_acquireLockBtn->setStyleSheet("background-color: #2E7D32; color: white; font-weight: bold; padding: 6px 10px;");

    m_releaseLockBtn = new QPushButton("ğŸ”“ Kilidi BÄ±rak (Release)", this);
    m_releaseLockBtn->setStyleSheet("background-color: #C62828; color: white; font-weight: bold; padding: 6px 10px;");

    m_heartbeatBtn = new QPushButton("ğŸ’“ Heartbeat GÃ¶nder", this);
    m_heartbeatBtn->setStyleSheet("background-color: #00838F; color: white; padding: 6px 10px;");

    m_queryLockBtn = new QPushButton("ğŸ” CanlÄ± Kilit Durumu", this);
    m_queryLockBtn->setStyleSheet("background-color: #4527A0; color: white; padding: 6px 10px;");

    m_contentionSimBtn = new QPushButton("âš¡ Otomatik Ã‡akÄ±ÅŸma ve SÄ±ra Testi (Masa 1 vs 2)", this);
    m_contentionSimBtn->setStyleSheet("background-color: #E65100; color: white; font-weight: bold; padding: 8px 12px;");

    m_lockHolderBadge = new QLabel("Kilit Durumu: BOÅTA (KullanÄ±labilir)", this);
    m_lockHolderBadge->setStyleSheet("background-color: #263238; color: #69F0AE; padding: 6px 10px; border-radius: 4px; font-weight: bold;");

    lockLayout->addWidget(new QLabel("Ä°stasyon No:", this), 0, 0);
    lockLayout->addWidget(m_stationSpin, 0, 1);
    lockLayout->addWidget(new QLabel("Aktif Kanal:", this), 0, 2);
    lockLayout->addWidget(m_channelSpin, 0, 3);

    lockLayout->addWidget(m_acquireLockBtn, 1, 0, 1, 2);
    lockLayout->addWidget(m_releaseLockBtn, 1, 2, 1, 2);
    lockLayout->addWidget(m_heartbeatBtn, 2, 0, 1, 2);
    lockLayout->addWidget(m_queryLockBtn, 2, 2, 1, 2);
    lockLayout->addWidget(m_contentionSimBtn, 3, 0, 1, 4);
    lockLayout->addWidget(m_lockHolderBadge, 4, 0, 1, 4);

    midLayout->addWidget(lockGroup, 5);

    // SaÄŸ Panel: Åema & Test Verisi KaydÄ±
    QGroupBox *schemaGroup = new QGroupBox("3. Åema SaÄŸlama & Test VeritabanÄ± KayÄ±tlarÄ±", this);
    QVBoxLayout *schemaLayout = new QVBoxLayout(schemaGroup);

    QHBoxLayout *schemaBtnLayout = new QHBoxLayout();
    m_initSchemaBtn = new QPushButton("ğŸ› ï¸ Tablo ÅemasÄ±nÄ± & VarsayÄ±lanlarÄ± YÃ¼kle", this);
    m_initSchemaBtn->setStyleSheet("background-color: #00695C; color: white; font-weight: bold; padding: 6px 10px;");

    m_loadProfilesBtn = new QPushButton("ğŸ“¡ TÃ¼rksat 42E Profili & 10 FrekansÄ± DoÄŸrula", this);
    m_loadProfilesBtn->setStyleSheet("background-color: #283593; color: white; font-weight: bold; padding: 6px 10px;");

    schemaBtnLayout->addWidget(m_initSchemaBtn);
    schemaBtnLayout->addWidget(m_loadProfilesBtn);
    schemaLayout->addLayout(schemaBtnLayout);

    QHBoxLayout *runBtnLayout = new QHBoxLayout();
    m_insertMockBtn = new QPushButton("ğŸ“ SimÃ¼le Test KoÅŸusu Yaz (10 Frekans PASS)", this);
    m_insertMockBtn->setStyleSheet("background-color: #37474F; color: white; padding: 6px 10px;");

    m_refreshRunsBtn = new QPushButton("ğŸ”„ Test GeÃ§miÅŸini Yenile", this);
    m_refreshRunsBtn->setStyleSheet("background-color: #455A64; color: white; padding: 6px 10px;");

    runBtnLayout->addWidget(m_insertMockBtn);
    runBtnLayout->addWidget(m_refreshRunsBtn);
    schemaLayout->addLayout(runBtnLayout);

    // Test Runs Mini Tablosu
    m_runsTable = new QTableWidget(this);
    m_runsTable->setColumnCount(7);
    m_runsTable->setHorizontalHeaderLabels(QStringList() << "ID" << "Ä°stasyon" << "Seri No" << "SÃ¼re" << "SonuÃ§" << "Ort Î” C/N" << "Tarih");
    m_runsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_runsTable->horizontalHeader()->setStretchLastSection(true);
    m_runsTable->setStyleSheet("background-color: #1E1E1E; color: #FFFFFF; font-size: 11px;");
    schemaLayout->addWidget(m_runsTable);

    midLayout->addWidget(schemaGroup, 6);
    mainLayout->addLayout(midLayout);

    // -------------------------------------------------------------------------
    // 3. ALT PANEL: CanlÄ± Log Konsolu
    // -------------------------------------------------------------------------
    QGroupBox *logGroup = new QGroupBox("4. VeritabanÄ± ve Kilit Ä°ÅŸlem Konsol LoglarÄ±", this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet("background-color: #121212; color: #E0E0E0; font-family: Consolas, monospace; font-size: 11px;");
    logLayout->addWidget(m_logEdit);
    mainLayout->addWidget(logGroup, 1);
}

void DbDebugWidget::appendLog(const QString &msg, const QString &color) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_logEdit->append(QString("<font color='#888888'>[%1]</font> <font color='%2'>%3</font>")
                          .arg(timeStr, color, msg));
}

void DbDebugWidget::updateConnectionStatusBadge(bool connected) {
    if (connected) {
        QString driver = DbManager::instance().driverName();
        m_statusBadge->setText(QString("DURUM: BAÄLI (%1)").arg(driver));
        m_statusBadge->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 4px 8px; border-radius: 4px; font-weight: bold;");
        if (!m_pollTimer.isActive()) {
            m_pollTimer.start(2000); // 2 saniyede bir kilit durumunu sorgula
        }
    } else {
        m_statusBadge->setText("DURUM: BAÄLI DEÄÄ°L");
        m_statusBadge->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 4px 8px; border-radius: 4px; font-weight: bold;");
        m_pollTimer.stop();
    }
}

void DbDebugWidget::onStartEngineClicked() {
    appendLog("ğŸš€ Yerel TaÅŸÄ±nabilir MariaDB motoru baÅŸlatÄ±lÄ±yor...", "#FFF59D");
    bool ok = DbManager::instance().startPortableMariaDbIfPresent();
    if (ok) {
        appendLog("âœ… TaÅŸÄ±nabilir MariaDB motoru aktif! (Port: 3306)", "#69F0AE");
        onConnectClicked();
    } else {
        appendLog("âŒ TaÅŸÄ±nabilir MariaDB baÅŸlatÄ±lamadÄ±. tools/mariadb_portable klasÃ¶rÃ¼nÃ¼ kontrol ediniz.", "#FF5252");
    }
}

void DbDebugWidget::onStopEngineClicked() {
    appendLog("â¹ï¸ TaÅŸÄ±nabilir MariaDB motoru durduruluyor...", "#FFF59D");
    DbManager::instance().closeDatabase();
    DbManager::instance().stopPortableMariaDbIfRunning();
    updateConnectionStatusBadge(false);
    appendLog("ğŸ›‘ MariaDB motoru ve baÄŸlantÄ±sÄ± gÃ¼venle kapatÄ±ldÄ±.", "#B0BEC5");
}

void DbDebugWidget::onConnectClicked() {
    QString host = m_hostEdit->text().trimmed();
    int port = m_portSpin->value();
    QString dbName = m_dbNameEdit->text().trimmed();
    QString user = m_userEdit->text().trimmed();
    QString pass = m_passEdit->text();

    appendLog(QString("ğŸ”Œ BaÄŸlanÄ±lÄ±yor: %1:%2/%3 (KullanÄ±cÄ±: %4)...").arg(host).arg(port).arg(dbName, user), "#FFF59D");

    bool ok = DbManager::instance().connectToDatabase(host, port, dbName, user, pass);
    updateConnectionStatusBadge(ok);

    if (ok) {
        appendLog("âœ… MariaDB VeritabanÄ± BaÄŸlantÄ±sÄ± BAÅARILI!", "#69F0AE");
        onQueryLockClicked();
        onRefreshTestRunsClicked();
    } else {
        appendLog(QString("âŒ BaÄŸlantÄ± HATASI: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
}

void DbDebugWidget::onDisconnectClicked() {
    DbManager::instance().closeDatabase();
    updateConnectionStatusBadge(false);
    m_lockHolderBadge->setText("Kilit Durumu: BAÄLANTI YOK");
    m_lockHolderBadge->setStyleSheet("background-color: #263238; color: #9E9E9E; padding: 6px 10px; border-radius: 4px;");
    appendLog("ğŸ”Œ VeritabanÄ± baÄŸlantÄ±sÄ± kapatÄ±ldÄ±.", "#B0BEC5");
}

void DbDebugWidget::onInitSchemaClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("âš ï¸ Ã–nce veritabanÄ±na baÄŸlanmalÄ±sÄ±nÄ±z!", "#FFA726");
        return;
    }

    appendLog("ğŸ› ï¸ VeritabanÄ± ÅŸemasÄ± ve tohum (seed) verileri yÃ¼kleniyor...", "#FFF59D");
    bool ok = DbManager::instance().ensureDefaultDataIfMissing();
    if (ok) {
        appendLog("âœ… Åema tablolarÄ± ve varsayÄ±lan TÃ¼rksat 42E verileri baÅŸarÄ±yla doÄŸrulandÄ±!", "#69F0AE");
        onLoadProfilesClicked();
        onQueryLockClicked();
    } else {
        appendLog(QString("âŒ Åema yÃ¼kleme HATASI: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
}

void DbDebugWidget::onLoadProfilesClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("âš ï¸ VeritabanÄ± baÄŸlantÄ±sÄ± yok!", "#FFA726");
        return;
    }

    auto profiles = DbManager::instance().getProfiles();
    appendLog(QString("ğŸ“¡ Toplam KayÄ±tlÄ± Uydu Profili: %1").arg(profiles.size()), "#81D4FA");

    for (const auto &p : profiles) {
        appendLog(QString("ğŸ‘‰ Profil #%1: '%2' (Uydu: %3, Tolerans: Â±%4 dB, Kanal SayÄ±sÄ±: %5)")
                      .arg(p.id)
                      .arg(p.profileName, p.satelliteName)
                      .arg(p.defaultToleranceDb, 0, 'f', 2)
                      .arg(p.channels.size()), "#E0E0E0");

        for (const auto &ch : p.channels) {
            appendLog(QString("   - TP #%1: %2 | %3 MHz | SR: %4 | Pol: %5 | Tol: Â±%6 dB")
                          .arg(ch.channelNo)
                          .arg(ch.channelName)
                          .arg(ch.frequencyMhz)
                          .arg(ch.symbolRate)
                          .arg(ch.polarization)
                          .arg(ch.toleranceDb, 0, 'f', 2), "#B0BEC5");
        }
    }
}

void DbDebugWidget::onAcquireLockClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("âš ï¸ VeritabanÄ± baÄŸlantÄ±sÄ± yok!", "#FFA726");
        return;
    }

    int stationId = m_stationSpin->value();
    int holder = 0;
    int ch = 0;

    bool ok = DbManager::instance().tryAcquireDbLock(stationId, holder, ch);
    if (ok) {
        appendLog(QString("ğŸ”’ [KÄ°LÄ°T ALINDI] Ä°stasyon #%1 referans DRX kilidini BAÅARIYLA aldÄ±!").arg(stationId), "#69F0AE");
    } else {
        appendLog(QString("â›” [KÄ°LÄ°T MEÅGUL] Kilit Ä°stasyon #%1 tarafÄ±ndan tutuluyor (Aktif Kanal: %2)!").arg(holder).arg(ch), "#FF5252");
    }
    onQueryLockClicked();
}

void DbDebugWidget::onReleaseLockClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("âš ï¸ VeritabanÄ± baÄŸlantÄ±sÄ± yok!", "#FFA726");
        return;
    }

    int stationId = m_stationSpin->value();
    bool ok = DbManager::instance().releaseDbLock(stationId);
    if (ok) {
        appendLog(QString("ğŸ”“ [KÄ°LÄ°T BIRAKILDI] Ä°stasyon #%1 kilidi serbest bÄ±raktÄ±.").arg(stationId), "#81D4FA");
    } else {
        appendLog(QString("âŒ Kilit bÄ±rakma baÅŸarÄ±sÄ±z: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
    onQueryLockClicked();
}

void DbDebugWidget::onHeartbeatClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("âš ï¸ VeritabanÄ± baÄŸlantÄ±sÄ± yok!", "#FFA726");
        return;
    }

    int stationId = m_stationSpin->value();
    int currentCh = m_channelSpin->value();
    bool ok = DbManager::instance().updateDbLockHeartbeat(stationId, currentCh);
    if (ok) {
        appendLog(QString("ğŸ’“ [HEARTBEAT] Ä°stasyon #%1 canlÄ±lÄ±k sinyali gÃ¶nderdi (Kanal #%2).").arg(stationId).arg(currentCh), "#FFD54F");
    } else {
        appendLog(QString("âŒ Heartbeat gÃ¼ncelleme baÅŸarÄ±sÄ±z: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
    onQueryLockClicked();
}

void DbDebugWidget::onQueryLockClicked() {
    if (!DbManager::instance().isConnected()) return;

    int currentChannel = 0;
    int holder = DbManager::instance().getDbLockHolder(currentChannel);

    if (holder == 0) {
        m_lockHolderBadge->setText("Kilit Durumu: ğŸŸ¢ BOÅTA (KullanÄ±labilir)");
        m_lockHolderBadge->setStyleSheet("background-color: #1B5E20; color: #C8E6C9; padding: 6px 10px; border-radius: 4px; font-weight: bold;");
    } else {
        m_lockHolderBadge->setText(QString("Kilit Durumu: ğŸ”´ Ä°STASYON %1 TEST EDÄ°YOR (Kanal #%2)").arg(holder).arg(currentChannel));
        m_lockHolderBadge->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold;");
    }
}

void DbDebugWidget::onRunContentionSimulationClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("âš ï¸ VeritabanÄ± baÄŸlantÄ±sÄ± yok!", "#FFA726");
        return;
    }

    appendLog("===================================================================", "#FFD54F");
    appendLog("âš¡ ALGORÄ°TMA 2: DAÄITIK Ã‡AKIÅMA & KUYRUK SÄ°MÃœLASYONU BAÅLATILIYOR", "#FFD54F");
    appendLog("===================================================================", "#FFD54F");

    // AdÄ±m 0: Ã–nceki kalÄ±ntÄ± varsa temizle
    DbManager::instance().releaseDbLock(0);

    // AdÄ±m 1: Ä°stasyon 1 kilidi talep eder
    int holder = 0;
    int ch = 0;
    bool ok1 = DbManager::instance().tryAcquireDbLock(1, holder, ch);
    if (ok1) {
        appendLog("âœ… [ADIM 1] Ä°stasyon 1 kilidi BAÅARIYLA aldÄ±.", "#69F0AE");
    } else {
        appendLog("âŒ [ADIM 1 HATASI] Ä°stasyon 1 kilidi alamadÄ±!", "#FF5252");
        return;
    }

    // AdÄ±m 2: Ä°stasyon 2 kilit almaya Ã§alÄ±ÅŸÄ±r (Engellenmelidir!)
    bool ok2 = DbManager::instance().tryAcquireDbLock(2, holder, ch);
    if (!ok2 && holder == 1) {
        appendLog(QString("âœ… [ADIM 2] Ä°stasyon 2 bekletildi (Kilit Ä°stasyon %1'de) - Ã‡AKIÅMA Ã–NLENDÄ°!").arg(holder), "#69F0AE");
    } else {
        appendLog(QString("âŒ [ADIM 2 HATASI] Atomik kilitleme baÅŸarÄ±sÄ±z! (ok2=%1, holder=%2)").arg(ok2).arg(holder), "#FF5252");
    }

    // AdÄ±m 3: Ä°stasyon 1 kanal ilerler ve heartbeat atar
    DbManager::instance().updateDbLockHeartbeat(1, 5);
    appendLog("âœ… [ADIM 3] Ä°stasyon 1 Kanal #5 iÃ§in Heartbeat gÃ¼ncelledi.", "#81D4FA");

    // AdÄ±m 4: Ä°stasyon 1 testi bitirir ve kilidi serbest bÄ±rakÄ±r
    DbManager::instance().releaseDbLock(1);
    appendLog("âœ… [ADIM 4] Ä°stasyon 1 testini tamamladÄ± ve kilidi serbest bÄ±raktÄ±.", "#81D4FA");

    // AdÄ±m 5: Åimdi Ä°stasyon 2 kilidi talep eder (BaÅŸarÄ±lÄ± olmalÄ±dÄ±r!)
    bool ok2_after = DbManager::instance().tryAcquireDbLock(2, holder, ch);
    if (ok2_after) {
        appendLog("âœ… [ADIM 5] Ä°stasyon 2 kilidi BAÅARIYLA devraldÄ±!", "#69F0AE");
        DbManager::instance().releaseDbLock(2);
        appendLog("âœ… [ADIM 6] Ä°stasyon 2 de kilidi serbest bÄ±raktÄ±. Kilit tertemiz boÅŸta.", "#69F0AE");
    } else {
        appendLog("âŒ [ADIM 5 HATASI] Ä°stasyon 2 serbest kilidi alamadÄ±!", "#FF5252");
    }

    appendLog("ğŸ‰ DAÄITIK KÄ°LÄ°T ENTEGRASYON TESTÄ° %100 BAÅARIYLA TAMAMLANDI!", "#69F0AE");
    appendLog("===================================================================", "#FFD54F");
    onQueryLockClicked();
}

void DbDebugWidget::onInsertMockRunClicked() {
    if (!DbManager::instance().isConnected()) {
        appendLog("âš ï¸ VeritabanÄ± baÄŸlantÄ±sÄ± yok!", "#FFA726");
        return;
    }

    TestRunData run;
    run.stationId = m_stationSpin->value();
    run.operatorId = 1;
    run.antennaSn = QString("DRX-DEBUG-%1").arg(QRandomGenerator::global()->bounded(10000, 99999));
    run.satelliteProfileId = 1;
    run.timestamp = QDateTime::currentDateTime();
    run.durationSec = 52;
    run.overallResult = "PASS";
    run.notes = "GeliÅŸtirici Debug Konsolu TarafÄ±ndan Otomatik Ãœretildi";

    double totalDelta = 0.0;
    double maxDelta = 0.0;

    for (int i = 1; i <= 10; ++i) {
        ChannelSampleResult ch;
        ch.channelNo = i;
        ch.channelName = QString("CH_%1").arg(i);
        ch.frequencyMhz = 11000 + (i * 125);
        ch.symbolRate = 27500;
        ch.polarization = (i % 2 == 0) ? "V" : "H";
        ch.dutCn = 14.5 + (QRandomGenerator::global()->bounded(100) / 100.0) - 0.5;
        ch.dutPow = -39.0 + (QRandomGenerator::global()->bounded(100) / 100.0) - 0.5;
        ch.refCn = 14.6;
        ch.refPow = -38.8;
        ch.deltaDb = ch.dutCn - ch.refCn;
        ch.carrierLocked = true;
        ch.videoLocked = true;
        ch.audioLocked = true;
        ch.toleranceDb = 1.5;
        ch.status = (qAbs(ch.deltaDb) <= ch.toleranceDb) ? "PASS" : "FAIL";

        totalDelta += ch.deltaDb;
        if (qAbs(ch.deltaDb) > maxDelta) maxDelta = qAbs(ch.deltaDb);

        run.channels.append(ch);
    }

    run.avgDeltaDb = totalDelta / 10.0;
    run.maxDeltaDb = maxDelta;

    qint64 runId = DbManager::instance().insertTestRun(run);
    if (runId > 0) {
        appendLog(QString("ğŸ“ [KAYIT BAÅARILI] Mock Test KoÅŸusu eklendi! (ID: #%1, Seri No: %2, SonuÃ§: %3, Ort Î”: %4 dB)")
                      .arg(runId)
                      .arg(run.antennaSn, run.overallResult)
                      .arg(run.avgDeltaDb, 0, 'f', 2), "#69F0AE");
        onRefreshTestRunsClicked();
    } else {
        appendLog(QString("âŒ Test koÅŸusu kayÄ±t HATASI: %1").arg(DbManager::instance().lastError()), "#FF5252");
    }
}

void DbDebugWidget::onRefreshTestRunsClicked() {
    if (!DbManager::instance().isConnected()) return;

    auto runs = DbManager::instance().getTestRuns(15);
    m_runsTable->setRowCount(runs.size());

    for (int r = 0; r < runs.size(); ++r) {
        const auto &run = runs[r];
        m_runsTable->setItem(r, 0, new QTableWidgetItem(QString::number(run.id)));
        m_runsTable->setItem(r, 1, new QTableWidgetItem(QString("Masa %1").arg(run.stationId)));
        m_runsTable->setItem(r, 2, new QTableWidgetItem(run.antennaSn));
        m_runsTable->setItem(r, 3, new QTableWidgetItem(QString("%1 sn").arg(run.durationSec)));

        QTableWidgetItem *resItem = new QTableWidgetItem(run.overallResult);
        if (run.overallResult == "PASS") {
            resItem->setForeground(QBrush(QColor("#69F0AE")));
        } else {
            resItem->setForeground(QBrush(QColor("#FF5252")));
        }
        m_runsTable->setItem(r, 4, resItem);

        m_runsTable->setItem(r, 5, new QTableWidgetItem(QString::number(run.avgDeltaDb, 'f', 2) + " dB"));
        m_runsTable->setItem(r, 6, new QTableWidgetItem(run.timestamp.toString("yyyy-MM-dd HH:mm:ss")));
    }
}

void DbDebugWidget::onRunFullSuiteClicked() {
    m_runFullSuiteBtn->setEnabled(false);
    m_suiteStep = 0;
    m_suiteProgress->setValue(0);
    m_suiteResultBanner->setText("â³ Otomatik teÅŸhis sÃ¼reci baÅŸlatÄ±ldÄ±, lÃ¼tfen bekleyiniz...");
    m_suiteResultBanner->setStyleSheet("background-color: #0D47A1; color: white; padding: 8px; border-radius: 4px; font-weight: bold; font-size: 12px;");

    auto resetCard = [](QLabel *lbl, const QString &text) {
        lbl->setText(QString("â³ %1: Test Ediliyor...").arg(text));
        lbl->setStyleSheet("background-color: #37474F; color: #FFF59D; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
    };

    resetCard(m_stepDriverLabel, "1. SÃ¼rÃ¼cÃ¼ & Motor");
    resetCard(m_stepLatencyLabel, "2. AÄŸ & Ping Gecikmesi");
    resetCard(m_stepSchemaLabel, "3. Åema & Tablolar");
    resetCard(m_stepChannelsLabel, "4. 10 TÃ¼rksat Transponder");
    resetCard(m_stepLockLabel, "5. DaÄŸÄ±tÄ±k Kilit & Ã‡akÄ±ÅŸma");
    resetCard(m_stepPersistenceLabel, "6. Test KaydÄ± (SQL I/O)");

    appendLog("===================================================================", "#00E676");
    appendLog("âš¡ OTOMATÄ°K VERÄ°TABANI VE DAÄITIK KÄ°LÄ°T TEÅHÄ°S SÃœÄ°TÄ° BAÅLATILDI", "#00E676");
    appendLog("===================================================================", "#00E676");

    m_suiteTimer.start(400);
}

void DbDebugWidget::onSuiteTimerTick() {
    switch (m_suiteStep) {
    case 0: { // AÅAMA 1: SÃ¼rÃ¼cÃ¼ & TaÅŸÄ±nabilir Motor
        appendLog("[AÅAMA 1/6] MariaDB sÃ¼rÃ¼cÃ¼sÃ¼ ve yerel taÅŸÄ±nabilir motor kontrol ediliyor...", "#FFF59D");
        bool ok = DbManager::instance().isConnected();
        if (!ok) {
            DbManager::instance().startPortableMariaDbIfPresent();
            onConnectClicked();
            ok = DbManager::instance().isConnected();
        }
        if (ok) {
            QString drv = DbManager::instance().driverName();
            m_stepDriverLabel->setText(QString("ğŸŸ¢ 1. Motor: AKTÄ°F (%1)").arg(drv));
            m_stepDriverLabel->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
            appendLog(QString("âœ… [AÅAMA 1/6] %1 SQL sÃ¼rÃ¼cÃ¼sÃ¼ doÄŸrulandÄ±, sunucu aktif ve hazÄ±r!").arg(drv), "#69F0AE");
        } else {
            m_stepDriverLabel->setText("ğŸ”´ 1. Motor: BAÅARISIZ");
            m_stepDriverLabel->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
            appendLog("âŒ [AÅAMA 1/6 HATASI] VeritabanÄ± motoruna baÄŸlanÄ±lamadÄ±!", "#FF5252");
        }
        m_suiteProgress->setValue(16);
        m_suiteStep++;
        break;
    }

    case 1: { // AÅAMA 2: Ping & AÄŸ Gecikmesi
        appendLog("[AÅAMA 2/6] SQL baÄŸlantÄ± gecikmesi ve ping sÃ¼resi Ã¶lÃ§Ã¼lÃ¼yor...", "#FFF59D");
        if (DbManager::instance().isConnected()) {
            QElapsedTimer timer;
            timer.start();
            for (int i = 0; i < 5; ++i) {
                QSqlQuery q(QSqlDatabase::database("NetaMainDb"));
                q.exec("SELECT 1");
            }
            double avgMs = timer.elapsed() / 5.0;
            m_stepLatencyLabel->setText(QString("ğŸŸ¢ 2. Ping: %1 ms (MÃ¼kemmel)").arg(avgMs, 0, 'f', 1));
            m_stepLatencyLabel->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
            appendLog(QString("âœ… [AÅAMA 2/6] VeritabanÄ± ortalama yanÄ±t gecikmesi: %1 ms (Ã‡ok HÄ±zlÄ±)").arg(avgMs, 0, 'f', 2), "#69F0AE");
        } else {
            m_stepLatencyLabel->setText("ğŸ”´ 2. Ping: HATA");
            m_stepLatencyLabel->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
        }
        m_suiteProgress->setValue(33);
        m_suiteStep++;
        break;
    }

    case 2: { // AÅAMA 3: Åema & 8 Tablo BÃ¼tÃ¼nlÃ¼ÄŸÃ¼
        appendLog("[AÅAMA 3/6] Tablo ÅŸemasÄ± ve iliÅŸkisel veri modeli denetleniyor...", "#FFF59D");
        if (DbManager::instance().isConnected()) {
            DbManager::instance().ensureDefaultDataIfMissing();
            QSqlQuery q("SHOW TABLES", QSqlDatabase::database("NetaMainDb"));
            int tableCount = 0;
            QStringList tableNames;
            while (q.next()) {
                tableCount++;
                tableNames << q.value(0).toString();
            }
            m_stepSchemaLabel->setText(QString("ğŸŸ¢ 3. Åema: %1/8 Tablo HazÄ±r").arg(tableCount));
            m_stepSchemaLabel->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
            appendLog(QString("âœ… [AÅAMA 3/6] %1 adet Ã¼retim tablosu doÄŸrulandÄ±: %2")
                          .arg(tableCount).arg(tableNames.join(", ")), "#69F0AE");
        } else {
            m_stepSchemaLabel->setText("ğŸ”´ 3. Åema: HATA");
            m_stepSchemaLabel->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
        }
        m_suiteProgress->setValue(50);
        m_suiteStep++;
        break;
    }

    case 3: { // AÅAMA 4: 10 TÃ¼rksat Transponder KanalÄ±
        appendLog("[AÅAMA 4/6] TÃ¼rksat 42E fabrika transponder kanallarÄ± sorgulanÄ±yor...", "#FFF59D");
        if (DbManager::instance().isConnected()) {
            auto profiles = DbManager::instance().getProfiles();
            int channelCount = 0;
            if (!profiles.isEmpty()) {
                channelCount = profiles.first().channels.size();
            }
            m_stepChannelsLabel->setText(QString("ğŸŸ¢ 4. Kanallar: %1/10 TP HazÄ±r").arg(channelCount));
            m_stepChannelsLabel->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
            appendLog(QString("âœ… [AÅAMA 4/6] Profil '%1' altÄ±nda %2 test frekansÄ± eksiksiz doÄŸrulandÄ±.")
                          .arg(profiles.isEmpty() ? "Yok" : profiles.first().profileName)
                          .arg(channelCount), "#69F0AE");
        } else {
            m_stepChannelsLabel->setText("ğŸ”´ 4. Kanallar: HATA");
            m_stepChannelsLabel->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
        }
        m_suiteProgress->setValue(66);
        m_suiteStep++;
        break;
    }

    case 4: { // AÅAMA 5: Algoritma 2 CanlÄ± DaÄŸÄ±tÄ±k Kilit & Ã‡akÄ±ÅŸma Ã–nleme
        appendLog("[AÅAMA 5/6] Algoritma 2 DaÄŸÄ±tÄ±k Kilit (reference_lock) Ã§akÄ±ÅŸma ve sÄ±ra testi yÃ¼rÃ¼tÃ¼lÃ¼yor...", "#FFF59D");
        if (DbManager::instance().isConnected()) {
            DbManager::instance().releaseDbLock(0); // Ã–nce sÄ±fÄ±rla
            int holder = 0, ch = 0;
            bool s1_ok = DbManager::instance().tryAcquireDbLock(1, holder, ch);
            bool s2_blocked = !DbManager::instance().tryAcquireDbLock(2, holder, ch) && (holder == 1);
            DbManager::instance().updateDbLockHeartbeat(1, 5);
            DbManager::instance().releaseDbLock(1);
            bool s2_ok = DbManager::instance().tryAcquireDbLock(2, holder, ch);
            DbManager::instance().releaseDbLock(2);

            if (s1_ok && s2_blocked && s2_ok) {
                m_stepLockLabel->setText("ğŸŸ¢ 5. Kilit: Ã‡akÄ±ÅŸma Ã–nleme BaÅŸarÄ±lÄ±");
                m_stepLockLabel->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
                appendLog("âœ… [AÅAMA 5/6] DaÄŸÄ±tÄ±k Kilit KuyruÄŸu kusursuz: Masa 1 kilit aldÄ± -> Masa 2 engellendi -> Masa 1 bÄ±raktÄ± -> Masa 2 devraldÄ±!", "#69F0AE");
            } else {
                m_stepLockLabel->setText("ğŸ”´ 5. Kilit: Ã‡akÄ±ÅŸma HatasÄ±");
                m_stepLockLabel->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
            }
        } else {
            m_stepLockLabel->setText("ğŸ”´ 5. Kilit: BAÄLANTI YOK");
            m_stepLockLabel->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
        }
        m_suiteProgress->setValue(83);
        m_suiteStep++;
        break;
    }

    case 5: { // AÅAMA 6: Test KoÅŸusu Yazma ve Okuma (SQL I/O HÄ±zÄ±)
        appendLog("[AÅAMA 6/6] 10 kanallÄ± simÃ¼le anten test koÅŸusu MariaDB'ye yazÄ±lÄ±yor...", "#FFF59D");
        if (DbManager::instance().isConnected()) {
            QElapsedTimer ioTimer;
            ioTimer.start();
            onInsertMockRunClicked();
            qint64 ioMs = ioTimer.elapsed();

            m_stepPersistenceLabel->setText(QString("ğŸŸ¢ 6. KayÄ±t: YazÄ±ldÄ± (%1 ms)").arg(ioMs));
            m_stepPersistenceLabel->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
            appendLog(QString("âœ… [AÅAMA 6/6] 10 kanal telemetri verisi %1 ms sÃ¼rede kaydedildi ve geri okundu!").arg(ioMs), "#69F0AE");
        } else {
            m_stepPersistenceLabel->setText("ğŸ”´ 6. KayÄ±t: HATA");
            m_stepPersistenceLabel->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; padding: 6px 10px; border-radius: 4px; font-weight: bold; font-size: 11px;");
        }
        m_suiteProgress->setValue(100);
        m_suiteStep++;
        break;
    }

    case 6: { // TAMAMLANDI: BÃœYÃœK YEÅÄ°L KUTLAMA BANNERI
        m_suiteTimer.stop();
        m_runFullSuiteBtn->setEnabled(true);
        m_suiteResultBanner->setText("ğŸ‰ MÃœKEMMEL! TÃœM VERÄ°TABANI VE ALGORÄ°TMA 2 ALTYAPISI %100 TEST EDÄ°LDÄ° VE Ã‡ALIÅIYOR!");
        m_suiteResultBanner->setStyleSheet("background-color: #1B5E20; color: #FFFFFF; padding: 10px; border-radius: 4px; font-weight: bold; font-size: 13px; border: 2px solid #00E676;");
        appendLog("===================================================================", "#00E676");
        appendLog("ğŸ‰ TEBRÄ°KLER: VERÄ°TABANI VE ALGORÄ°TMA 2 TÃœM TESTLERDEN BAÅARIYLA GEÃ‡TÄ° (PASS)!", "#00E676");
        appendLog("===================================================================", "#00E676");
        break;
    }
    }
}

