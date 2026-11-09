/**
 * @file debug_runner_window.cpp
 * @brief Implementation of the interactive Developer and Debug Test Console.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "debug_runner_window.h"
#include "db_debug_widget.h"
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

    // Sinyal / Slot bağlantıları
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
        onErrorOccurred("Login Hatası: " + reason);
    });
    connect(m_webClient, &DrxWebClient::tuneCompleted, this, &DebugRunnerWindow::onTuneFinished);
    connect(m_webClient, &DrxWebClient::statusReceived, this, [this](bool s, bool v, bool a, const QString &ch) {
        m_carrierLabel->setText(QString("Taşıyıcı (S): %1").arg(s ? "<font color='#4CAF50'><b>KİLİTLİ (S:+)</b></font>" : "<font color='#F44336'>KİLİTSİZ (S:-)</font>"));
        m_videoLabel->setText(QString("Video (V): %1").arg(v ? "<font color='#4CAF50'><b>VAR (V:+)</b></font>" : "<font color='#F44336'>YOK (V:-)</font>"));
        m_audioLabel->setText(QString("Ses (A): %1").arg(a ? "<font color='#4CAF50'><b>VAR (A:+)</b></font>" : "<font color='#F44336'>YOK (A:-)</font>"));
        if (!ch.isEmpty()) {
            m_statusLabel->setText(QString("<b>Kanal:</b> <font color='#81D4FA'>%1</font>").arg(ch));
        }
    });
    connect(m_webClient, &DrxWebClient::telemetryReceived, this, [this](double powDbm, double cnDb, double ber, double ebN0) {
        m_liveCnLabel->setText(QString("<b>C/N:</b> %1 dB").arg(QString::number(cnDb, 'f', 2)));
        m_livePowLabel->setText(QString("<b>Güç:</b> %1 dBm").arg(QString::number(powDbm, 'f', 2)));
        m_liveEbN0Label->setText(QString("<b>Eb/N0:</b> %1 dB").arg(QString::number(ebN0, 'f', 2)));
        m_liveBerLabel->setText(QString("<b>BER:</b> %1").arg(ber > 0 ? QString::number(ber, 'e', 1) : "-"));

        if (m_isSampling) {
            m_cnSamples.append(cnDb);
            m_powSamples.append(powDbm);
            appendLog(QString("[Örnek #%1] C/N: %2 dB | Güç: %3 dBm | Eb/N0: %4 dB | BER: %5")
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

    appendLog("🚀 DEBUG TEST MODU Başlatıldı. Cihaz IP'sini belirleyip 'Bağlan & Login' butonuna basınız.", "#4CAF50");
}

DebugRunnerWindow::~DebugRunnerWindow() {
    m_sampleTickTimer.stop();
    m_sampleDurationTimer.stop();
}

void DebugRunnerWindow::setupUi() {
    setWindowTitle("NETA DRX9000 - Geliştirici & REST API Debug Test Konsolu");
    resize(1020, 750);
    setStyleSheet(StyleHelper::getAppStyle());

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(14, 14, 14, 14);

    // 1. ÜST PANEL: Cihaz ve Login Bilgileri
    QGroupBox *connGroup = new QGroupBox("1. DRX Web Sunucusu Bağlantı ve Giriş Bilgileri", this);
    QHBoxLayout *connLayout = new QHBoxLayout(connGroup);
    m_ipEdit = new QLineEdit("192.168.0.167", this);
    m_userEdit = new QLineEdit("admin", this);
    m_passEdit = new QLineEdit("1234", this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_connectBtn = new QPushButton("🔑 Bağlan & Login (SessionKey Al)", this);
    m_connectBtn->setStyleSheet("background-color: #1976D2; color: white; font-weight: bold; padding: 6px 12px;");

    m_scanAllBtn = new QPushButton("🔍 Tüm Sayfaları Tara (Tüm Bilgileri Getir)", this);
    m_scanAllBtn->setStyleSheet("background-color: #0288D1; color: white; font-weight: bold; padding: 6px 12px;");

    connLayout->addWidget(new QLabel("IP:", this));
    connLayout->addWidget(m_ipEdit);
    connLayout->addWidget(new QLabel("Kullanıcı:", this));
    connLayout->addWidget(m_userEdit);
    connLayout->addWidget(new QLabel("Şifre:", this));
    connLayout->addWidget(m_passEdit);
    connLayout->addWidget(m_connectBtn);
    connLayout->addWidget(m_scanAllBtn);
    mainLayout->addWidget(connGroup);

    // 2. ORTA PANEL: Frekans Zıplama & Örnekleme Parametreleri
    QGroupBox *testGroup = new QGroupBox("2. Frekans Ayarlama ve Örnekleme Testi", this);
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

    m_tuneBtn = new QPushButton("🎯 Frekansa Zıpla (ResponseTo=freq)", this);
    m_tuneBtn->setStyleSheet("background-color: #00897B; color: white; font-weight: bold; padding: 6px 10px;");

    m_pollBtn = new QPushButton("📡 Tek Seferlik Telemetri Oku (alarmStatus)", this);
    m_pollBtn->setStyleSheet("background-color: #5E35B1; color: white; padding: 6px 10px;");

    m_startSampleBtn = new QPushButton("⏱️ 5 sn Örnekle & Ortalama C/N Hesapla", this);
    m_startSampleBtn->setStyleSheet("background-color: #E65100; color: white; font-weight: bold; padding: 8px 12px; font-size: 13px;");

    testLayout->addWidget(new QLabel("Frekans:", this), 0, 0);
    testLayout->addWidget(m_freqSpin, 0, 1);
    testLayout->addWidget(new QLabel("Sembol Oranı:", this), 0, 2);
    testLayout->addWidget(m_srSpin, 0, 3);
    testLayout->addWidget(new QLabel("Polarizasyon:", this), 0, 4);
    testLayout->addWidget(m_polCombo, 0, 5);

    testLayout->addWidget(m_tuneBtn, 1, 0, 1, 2);
    testLayout->addWidget(m_pollBtn, 1, 2, 1, 2);
    testLayout->addWidget(new QLabel("Örnekleme Süresi:", this), 1, 4);
    testLayout->addWidget(m_sampleDurationSpin, 1, 5);

    testLayout->addWidget(m_startSampleBtn, 2, 0, 1, 6);
    mainLayout->addWidget(testGroup);

    // 3. CANLI TELEMETRİ VE ORTALAMA SONUÇ PANELİ
    QGroupBox *statusGroup = new QGroupBox("3. Canlı Telemetri ve Ortalama Göstergeleri", this);
    QHBoxLayout *statusLayout = new QHBoxLayout(statusGroup);

    m_statusLabel = new QLabel("<b>Kanal:</b> -", this);
    m_carrierLabel = new QLabel("Taşıyıcı (S): -", this);
    m_videoLabel = new QLabel("Video (V): -", this);
    m_audioLabel = new QLabel("Ses (A): -", this);
    m_liveCnLabel = new QLabel("<b>C/N:</b> - dB", this);
    m_livePowLabel = new QLabel("<b>Güç:</b> - dBm", this);
    m_liveEbN0Label = new QLabel("<b>Eb/N0:</b> - dB", this);
    m_liveBerLabel = new QLabel("<b>BER:</b> -", this);
    m_avgResultLabel = new QLabel("<b>[ORTALAMA]:</b> Henüz hesaplanmadı", this);
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

    // 4. TAB PANEL: DETAYLI LOGLAR & HAM JSON ÇIKTISI
    m_tabWidget = new QTabWidget(this);

    // Tab 1: Geliştirici Konsol Logları
    QWidget *tabLog = new QWidget(this);
    QVBoxLayout *tabLogLayout = new QVBoxLayout(tabLog);
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet("background-color: #121212; color: #E0E0E0; font-family: Consolas, monospace; font-size: 11px;");
    tabLogLayout->addWidget(m_logEdit);
    m_tabWidget->addTab(tabLog, "📑 Ayrıştırılmış Geliştirici Logları");

    // Tab 2: Ham Web Yanıtı (Raw JSON)
    QWidget *tabRaw = new QWidget(this);
    QVBoxLayout *tabRawLayout = new QVBoxLayout(tabRaw);
    QHBoxLayout *rawTopBar = new QHBoxLayout();
    m_copyRawBtn = new QPushButton("📋 Ham JSON'ı Panoya Kopyala", this);
    m_copyRawBtn->setStyleSheet("background-color: #37474F; color: white; font-weight: bold; padding: 4px 10px;");
    QPushButton *clearRawBtn = new QPushButton("🧹 Temizle", this);
    connect(clearRawBtn, &QPushButton::clicked, this, [this]() { m_rawJsonEdit->clear(); });

    rawTopBar->addWidget(new QLabel("<b>DRX Web Sunucusundan Gelen Ham JSON / Metin Yanıtı:</b>", this));
    rawTopBar->addStretch();
    rawTopBar->addWidget(m_copyRawBtn);
    rawTopBar->addWidget(clearRawBtn);
    tabRawLayout->addLayout(rawTopBar);

    m_rawJsonEdit = new QTextEdit(this);
    m_rawJsonEdit->setReadOnly(true);
    m_rawJsonEdit->setStyleSheet("background-color: #0D1117; color: #58A6FF; font-family: Consolas, monospace; font-size: 12px;");
    tabRawLayout->addWidget(m_rawJsonEdit);
    m_tabWidget->addTab(tabRaw, "📋 DRX Ham JSON Yanıtı (Raw Payload)");

    // Tab 3: MariaDB ve Dağıtık Kilit (Algoritma 2) Konsolu
    m_dbDebugWidget = new DbDebugWidget(this);
    m_tabWidget->addTab(m_dbDebugWidget, "🗄️ MariaDB & Dağıtık Kilit (Algoritma 2)");

    mainLayout->addWidget(m_tabWidget);
}

void DebugRunnerWindow::onConnectClicked() {
    m_webClient->setHost(m_ipEdit->text().trimmed());
    m_webClient->setCredentials(m_userEdit->text().trimmed(), m_passEdit->text().trimmed());
    appendLog(QString("🔌 %1 cihazına bağlanılıyor...").arg(m_ipEdit->text()), "#FFD54F");
    m_webClient->login();
}

void DebugRunnerWindow::onTuneClicked() {
    appendLog(QString("🎯 Frekansa Zıplanıyor -> %1 MHz, SR: %2, Pol: %3")
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
    m_avgResultLabel->setText("⏳ Örnekleme yapılıyor, bekleniyor...");
    appendLog(QString("⏱️ %1 saniyelik çoklu veri örneklemesi başladı (250 ms aralıkla)...").arg(durationSec), "#FFA726");
}

void DebugRunnerWindow::onSampleTimerTick() {
    m_webClient->requestStatus();
}

void DebugRunnerWindow::onSamplingFinished() {
    m_sampleTickTimer.stop();
    m_isSampling = false;
    m_startSampleBtn->setEnabled(true);

    if (m_cnSamples.isEmpty()) {
        appendLog("⚠️ Hiç C/N örneği alınamadı!", "#FF5252");
        m_avgResultLabel->setText("<b>[HATA]:</b> Veri alınamadı!");
        return;
    }

    double sumCn = std::accumulate(m_cnSamples.begin(), m_cnSamples.end(), 0.0);
    double avgCn = sumCn / m_cnSamples.size();

    double sumPow = std::accumulate(m_powSamples.begin(), m_powSamples.end(), 0.0);
    double avgPow = sumPow / m_powSamples.size();

    QString summary = QString("<b>[ORTALAMA (%1 Örnek)]:</b> C/N: <font color='#4CAF50'><b>%2 dB</b></font> | Güç: <font color='#4DD0E1'><b>%3 dBm</b></font>")
                          .arg(m_cnSamples.size())
                          .arg(QString::number(avgCn, 'f', 2))
                          .arg(QString::number(avgPow, 'f', 2));

    m_avgResultLabel->setText(summary);
    appendLog(QString("✅ Örnekleme Tamamlandı! Ortalama C/N: %1 dB | Ortalama Güç: %2 dBm (Toplam %3 örnek)")
                  .arg(QString::number(avgCn, 'f', 2))
                  .arg(QString::number(avgPow, 'f', 2))
                  .arg(m_cnSamples.size()), "#69F0AE");
}

void DebugRunnerWindow::onSessionKeyReceived(const QString &sessionKey) {
    appendLog(QString("🔑 Oturum Açıldı! Alınan SessionKey: %1").arg(sessionKey), "#81C784");
}

void DebugRunnerWindow::onTuneFinished(bool success, const QString &message) {
    if (success) {
        appendLog("✅ Frekans Ayarlandı: " + message, "#A5D6A7");
    } else {
        appendLog("❌ Frekans Ayarlama Başarısız: " + message, "#EF5350");
    }
}

void DebugRunnerWindow::onErrorOccurred(const QString &error) {
    appendLog("🚨 " + error, "#FF1744");
}

void DebugRunnerWindow::onRawResponseReceived(const QString &actionType, const QByteArray &rawJson) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    
    // JSON'ı girintili (indented) formatta güzelleştir
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(rawJson, &parseErr);
    QString prettyJson;
    if (parseErr.error == QJsonParseError::NoError) {
        prettyJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    } else {
        prettyJson = QString::fromUtf8(rawJson);
    }

    QString header = QString("======================================================================\n"
                             "[%1] İSTEK TÜRÜ: %2 | BOYUT: %3 bayt\n"
                             "======================================================================\n")
                         .arg(timeStr, actionType).arg(rawJson.size());

    m_rawJsonEdit->append(header + prettyJson + "\n\n");
}

void DebugRunnerWindow::onScanAllClicked() {
    appendLog("🔍 Cihazın tüm olası sayfaları (alarmStatus, channeldetails, frequency, deviceInfo) sorgulanıyor...", "#AB47BC");
    m_webClient->requestStatus();
    m_webClient->requestDeviceInfo();
    m_webClient->sendRawQuery("channeldetails");
    m_webClient->sendRawQuery("frequency");
    m_webClient->sendRawQuery("channelselection");
    
    // Kullanıcıyı doğrudan Ham JSON sekmesine geçir
    m_tabWidget->setCurrentIndex(1);
}

void DebugRunnerWindow::onCopyRawClicked() {
    if (m_rawJsonEdit) {
        QApplication::clipboard()->setText(m_rawJsonEdit->toPlainText());
        appendLog("📋 Ham JSON içeriği panoya kopyalandı!", "#80CBC4");
    }
}

void DebugRunnerWindow::appendLog(const QString &msg, const QString &color) {
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_logEdit->append(QString("<font color='#78909C'>[%1]</font> <font color='%2'>%3</font>")
                          .arg(timeStr, color, msg));
}
