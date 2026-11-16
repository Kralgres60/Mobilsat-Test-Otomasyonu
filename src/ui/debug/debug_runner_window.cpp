/**
 * @file debug_runner_window.cpp
 * @brief Streamlined developer debug console focused on DRX REST API requests and JSON outputs.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "debug_runner_window.h"
#include "db_debug_window.h"
#include "hardware/drx_protocol_config.h"
#include "core/app_config.h"
#include "ui/common/style_helper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QDateTime>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <numeric>

DebugRunnerWindow::DebugRunnerWindow(QWidget *parent)
    : QMainWindow(parent), m_webClient(new Ethernet(this)) {
    setupUi();

    connect(&m_sampleTickTimer, &QTimer::timeout, this, &DebugRunnerWindow::onSampleTimerTick);
    connect(&m_sampleDurationTimer, &QTimer::timeout, this, &DebugRunnerWindow::onSamplingFinished);
    m_sampleDurationTimer.setSingleShot(true);

    // Wire Ethernet Web CGI Signals
    connect(m_webClient, &Ethernet::loginSucceeded, this, &DebugRunnerWindow::onSessionKeyReceived);
    connect(m_webClient, &Ethernet::loginFailed, this, &DebugRunnerWindow::onLoginFailed);
    connect(m_webClient, &Ethernet::tuneCompleted, this, &DebugRunnerWindow::onTuneFinished);
    connect(m_webClient, &Ethernet::rawResponseReceived, this, &DebugRunnerWindow::onRawResponseReceived);
    connect(m_webClient, &Ethernet::telemetryReceived, this, &DebugRunnerWindow::onTelemetryReceived);
    connect(m_webClient, &Ethernet::statusReceived, this, &DebugRunnerWindow::onStatusReceived);
    connect(m_webClient, &Ethernet::timestampReceived, this, &DebugRunnerWindow::onTimestampReceived);
    connect(m_webClient, &Ethernet::pingFinished, this, [this](bool reachable, int latencyMs, const QString &host) {
        if (reachable) {
            appendLog(QString("✔ Ping başarılı: %1 (%2 ms)").arg(host).arg(latencyMs), "#34D399");
        } else {
            appendLog(QString("❌ Ping yanıt vermiyor: %1").arg(host), "#EF4444");
        }
    });
    connect(m_webClient, &Ethernet::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog(msg, isErr ? "#EF4444" : "#94A3B8");
    });

    appendLog("🚀 DRX REST API Debug Konsolu hazır. Cihaz IP'si ile bağlanıp test yapabilirsiniz.", "#34D399");
}

DebugRunnerWindow::~DebugRunnerWindow() {
    m_sampleTickTimer.stop();
    m_sampleDurationTimer.stop();
}

void DebugRunnerWindow::setupUi() {
    setWindowTitle("NETA DRX9000 - REST API & Donanım Debug Konsolu");
    resize(1080, 720);
    setStyleSheet("background-color: #1E1E2E; color: #E0E0E0; font-family: 'Segoe UI', Arial, sans-serif;");

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    // =========================================================================
    // 1. Top Connection Bar
    // =========================================================================
    QGroupBox *grpConn = new QGroupBox("🌐 DRX Web Server (REST CGI / Ethernet Bağlantısı)", central);
    grpConn->setStyleSheet("QGroupBox { font-weight: bold; color: #89DCEB; border: 1px solid #45475A; "
                           "border-radius: 6px; margin-top: 6px; padding-top: 10px; } "
                           "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    QHBoxLayout *hConn = new QHBoxLayout(grpConn);
    hConn->setContentsMargins(10, 8, 10, 8);
    hConn->setSpacing(8);

    const AppConfig &cfg = AppConfig::instance();

    m_txtIp = new QLineEdit(cfg.stationDrxHost(), grpConn);
    m_txtIp->setPlaceholderText("IP (192.168.0.167)");
    m_txtIp->setFixedWidth(130);

    m_spinPort = new QSpinBox(grpConn);
    m_spinPort->setRange(1, 65535);
    m_spinPort->setValue(cfg.stationDrxPort() > 0 ? cfg.stationDrxPort() : 80);
    m_spinPort->setFixedWidth(70);

    m_txtUser = new QLineEdit(cfg.drxUsername(), grpConn);
    m_txtUser->setPlaceholderText("Kullanıcı");
    m_txtUser->setFixedWidth(90);

    m_txtPass = new QLineEdit(cfg.drxPassword(), grpConn);
    m_txtPass->setEchoMode(QLineEdit::Password);
    m_txtPass->setPlaceholderText("Şifre");
    m_txtPass->setFixedWidth(90);

    m_btnConnect = new QPushButton("🔌 Bağlan & Login", grpConn);
    m_btnConnect->setStyleSheet(StyleHelper::getPastelGreenStyle());
    m_btnConnect->setCursor(Qt::PointingHandCursor);
    connect(m_btnConnect, &QPushButton::clicked, this, &DebugRunnerWindow::onConnectClicked);

    m_btnPing = new QPushButton("📶 Ping", grpConn);
    m_btnPing->setStyleSheet(StyleHelper::getPastelBlueStyle());
    m_btnPing->setCursor(Qt::PointingHandCursor);
    connect(m_btnPing, &QPushButton::clicked, this, &DebugRunnerWindow::onPingClicked);

    m_lblSessionBadge = new QLabel("🔑 Oturum: Yok", grpConn);
    m_lblSessionBadge->setStyleSheet("background-color: #313244; color: #CDD6F4; padding: 4px 10px; border-radius: 4px; font-weight: bold;");

    m_btnOpenDbTool = new QPushButton("🗄️ Veritabanı Aracı", grpConn);
    m_btnOpenDbTool->setStyleSheet(StyleHelper::getPastelAmberStyle());
    m_btnOpenDbTool->setCursor(Qt::PointingHandCursor);
    connect(m_btnOpenDbTool, &QPushButton::clicked, this, &DebugRunnerWindow::onOpenDbToolClicked);

    hConn->addWidget(new QLabel("IP:", grpConn));
    hConn->addWidget(m_txtIp);
    hConn->addWidget(new QLabel("Port:", grpConn));
    hConn->addWidget(m_spinPort);
    hConn->addWidget(new QLabel("Kullanıcı:", grpConn));
    hConn->addWidget(m_txtUser);
    hConn->addWidget(new QLabel("Şifre:", grpConn));
    hConn->addWidget(m_txtPass);
    hConn->addWidget(m_btnConnect);
    hConn->addWidget(m_btnPing);
    hConn->addWidget(m_lblSessionBadge);
    hConn->addStretch();
    m_topClockBadge = new DrxClockBadge(grpConn);
    hConn->addWidget(m_topClockBadge);
    hConn->addWidget(m_btnOpenDbTool);

    mainLayout->addWidget(grpConn);

    // =========================================================================
    // 2. Main Middle Area: Splitter (Left: Requests & Controls | Right: Output)
    // =========================================================================
    QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setHandleWidth(4);

    // -------------------------------------------------------------------------
    // Left Widget: Requests, Tuning, Telemetry Controls
    // -------------------------------------------------------------------------
    QWidget *wLeft = new QWidget(splitter);
    QVBoxLayout *vLeft = new QVBoxLayout(wLeft);
    vLeft->setContentsMargins(0, 0, 4, 0);
    vLeft->setSpacing(8);

    // Group: Frequency Tuning
    QGroupBox *grpTune = new QGroupBox("📡 Transponder / Frekans Ayarlama (POST freq)", wLeft);
    grpTune->setStyleSheet("QGroupBox { font-weight: bold; color: #FAB387; border: 1px solid #45475A; "
                           "border-radius: 6px; margin-top: 6px; padding: 8px; }");
    QGridLayout *gridTune = new QGridLayout(grpTune);
    gridTune->setContentsMargins(8, 8, 8, 8);
    gridTune->setSpacing(6);

    m_spinFreq = new QSpinBox(grpTune);
    m_spinFreq->setRange(950, 21500);
    m_spinFreq->setValue(11054);
    m_spinFreq->setSuffix(" MHz");

    m_spinSymbolRate = new QSpinBox(grpTune);
    m_spinSymbolRate->setRange(1000, 45000);
    m_spinSymbolRate->setValue(30000);
    m_spinSymbolRate->setSuffix(" kS/s");

    m_comboPol = new QComboBox(grpTune);
    m_comboPol->addItems({"Dikey (V) / 13V", "Yatay (H) / 18V"});

    m_btnTune = new QPushButton("🚀 Frekansı Cihaza Bas", grpTune);
    m_btnTune->setStyleSheet(StyleHelper::getPastelGreenStyle());
    m_btnTune->setCursor(Qt::PointingHandCursor);
    connect(m_btnTune, &QPushButton::clicked, this, &DebugRunnerWindow::onTuneClicked);

    gridTune->addWidget(new QLabel("Frekans:", grpTune), 0, 0);
    gridTune->addWidget(m_spinFreq, 0, 1);
    gridTune->addWidget(new QLabel("Sembol Oranı:", grpTune), 0, 2);
    gridTune->addWidget(m_spinSymbolRate, 0, 3);
    gridTune->addWidget(new QLabel("Polarizasyon:", grpTune), 1, 0);
    gridTune->addWidget(m_comboPol, 1, 1);
    gridTune->addWidget(m_btnTune, 1, 2, 1, 2);
    vLeft->addWidget(grpTune);

    // Group: Live Telemetry Badges & Queries
    QGroupBox *grpTele = new QGroupBox("📊 Telemetri ve Durum Sorgulama (alarmStatus)", wLeft);
    grpTele->setStyleSheet("QGroupBox { font-weight: bold; color: #A6E3A1; border: 1px solid #45475A; "
                           "border-radius: 6px; margin-top: 6px; padding: 8px; }");
    QVBoxLayout *vTele = new QVBoxLayout(grpTele);
    vTele->setContentsMargins(8, 8, 8, 8);
    vTele->setSpacing(6);

    QHBoxLayout *hTeleBtns = new QHBoxLayout();
    m_btnAlarmStatus = new QPushButton("🔍 alarmStatus Sorgula", grpTele);
    m_btnAlarmStatus->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnAlarmStatus, &QPushButton::clicked, this, &DebugRunnerWindow::onPollAlarmStatusClicked);

    m_btnDeviceInfo = new QPushButton("ℹ️ deviceInfo Sorgula", grpTele);
    m_btnDeviceInfo->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnDeviceInfo, &QPushButton::clicked, this, &DebugRunnerWindow::onPollDeviceInfoClicked);

    m_spinSampleSec = new QSpinBox(grpTele);
    m_spinSampleSec->setRange(1, 60);
    m_spinSampleSec->setValue(5);
    m_spinSampleSec->setSuffix(" sn");

    m_btnStartSample = new QPushButton("⏱️ Örnekleme Başlat", grpTele);
    m_btnStartSample->setStyleSheet(StyleHelper::getPastelAmberStyle());
    connect(m_btnStartSample, &QPushButton::clicked, this, &DebugRunnerWindow::onStartSamplingClicked);

    hTeleBtns->addWidget(m_btnAlarmStatus);
    hTeleBtns->addWidget(m_btnDeviceInfo);
    hTeleBtns->addWidget(m_spinSampleSec);
    hTeleBtns->addWidget(m_btnStartSample);
    vTele->addLayout(hTeleBtns);

    // Live Indicators Grid
    QGridLayout *gridBadges = new QGridLayout();
    gridBadges->setSpacing(6);

    auto makeBadge = [](const QString &text, const QString &bg) {
        QLabel *l = new QLabel(text);
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(QString("background-color: %1; color: #CDD6F4; padding: 6px; border-radius: 4px; font-weight: bold;").arg(bg));
        return l;
    };

    m_lblCarrier = makeBadge("Taşıyıcı: Beklemede", "#313244");
    m_lblVideo = makeBadge("Video: Beklemede", "#313244");
    m_lblAudio = makeBadge("Audio: Beklemede", "#313244");
    m_lblChannelName = makeBadge("Kanal: -", "#1E1E2E");
    m_lblCn = makeBadge("C/N: -- dB", "#181825");
    m_lblPower = makeBadge("Güç: -- dBm", "#181825");
    m_lblBer = makeBadge("BER: --", "#181825");
    m_lblEbN0 = makeBadge("Eb/N0: -- dB", "#181825");
    m_lblDeviceTime = makeBadge("Cihaz Saati: --", "#1E1E2E");
    m_lblDeltaTime = makeBadge("Δt (Gecikme): -- ms", "#1E1E2E");

    gridBadges->addWidget(m_lblCarrier, 0, 0);
    gridBadges->addWidget(m_lblVideo, 0, 1);
    gridBadges->addWidget(m_lblAudio, 0, 2);
    gridBadges->addWidget(m_lblChannelName, 0, 3);
    gridBadges->addWidget(m_lblCn, 1, 0);
    gridBadges->addWidget(m_lblPower, 1, 1);
    gridBadges->addWidget(m_lblBer, 1, 2);
    gridBadges->addWidget(m_lblEbN0, 1, 3);
    gridBadges->addWidget(m_lblDeviceTime, 2, 0, 1, 2);
    gridBadges->addWidget(m_lblDeltaTime, 2, 2, 1, 2);
    vTele->addLayout(gridBadges);
    vLeft->addWidget(grpTele);

    // Group: Custom CGI POST Sender
    QGroupBox *grpCustom = new QGroupBox("🛠️ Özel REST CGI POST Gönderici (/cgi-bin/drx.cgi)", wLeft);
    grpCustom->setStyleSheet("QGroupBox { font-weight: bold; color: #CBA6F7; border: 1px solid #45475A; "
                            "border-radius: 6px; margin-top: 6px; padding: 8px; }");
    QVBoxLayout *vCustom = new QVBoxLayout(grpCustom);
    vCustom->setContentsMargins(8, 8, 8, 8);
    vCustom->setSpacing(6);

    QHBoxLayout *hCustomHead = new QHBoxLayout();
    m_comboCgiAction = new QComboBox(grpCustom);
    m_comboCgiAction->addItems({"alarmStatus", "deviceInfo", "freq", "login", "raw"});
    connect(m_comboCgiAction, QOverload<const QString &>::of(&QComboBox::currentTextChanged), this, &DebugRunnerWindow::setPresetPayloadForAction);

    m_btnSendCustom = new QPushButton("📤 İsteği Gönder (POST)", grpCustom);
    m_btnSendCustom->setStyleSheet(StyleHelper::getPastelGreenStyle());
    connect(m_btnSendCustom, &QPushButton::clicked, this, &DebugRunnerWindow::onSendCustomPostClicked);

    hCustomHead->addWidget(new QLabel("İstek Şablonu:", grpCustom));
    hCustomHead->addWidget(m_comboCgiAction);
    hCustomHead->addStretch();
    hCustomHead->addWidget(m_btnSendCustom);
    vCustom->addLayout(hCustomHead);

    m_txtCustomPayload = new QTextEdit(grpCustom);
    m_txtCustomPayload->setFixedHeight(80);
    m_txtCustomPayload->setStyleSheet("background-color: #11111B; border: 1px solid #313244; color: #CDD6F4; "
                                     "font-family: 'Consolas', monospace; font-size: 11px;");
    vCustom->addWidget(m_txtCustomPayload);
    vLeft->addWidget(grpCustom);

    setPresetPayloadForAction("alarmStatus");
    splitter->addWidget(wLeft);

    // -------------------------------------------------------------------------
    // Right Widget: Raw Output Panel (Core Focus)
    // -------------------------------------------------------------------------
    QWidget *wRight = new QWidget(splitter);
    QVBoxLayout *vRight = new QVBoxLayout(wRight);
    vRight->setContentsMargins(4, 0, 0, 0);
    vRight->setSpacing(8);

    QGroupBox *grpOutput = new QGroupBox("📥 DRX REST API Yanıt Çıktısı (JSON / Raw Response)", wRight);
    grpOutput->setStyleSheet("QGroupBox { font-weight: bold; color: #89B4FA; border: 1px solid #45475A; "
                            "border-radius: 6px; margin-top: 6px; padding: 8px; }");
    QVBoxLayout *vOut = new QVBoxLayout(grpOutput);
    vOut->setContentsMargins(8, 8, 8, 8);
    vOut->setSpacing(6);

    QHBoxLayout *hOutBar = new QHBoxLayout();
    m_lblResponseStatus = new QLabel("HTTP Durum: Bekleniyor...", grpOutput);
    m_lblResponseStatus->setStyleSheet("background-color: #313244; color: #CDD6F4; padding: 4px 8px; border-radius: 4px; font-weight: bold;");

    m_lblResponseLatency = new QLabel("Gecikme: -- ms", grpOutput);
    m_lblResponseLatency->setStyleSheet("background-color: #181825; color: #89DCEB; padding: 4px 8px; border-radius: 4px;");

    m_btnCopyRaw = new QPushButton("📋 JSON Kopyala", grpOutput);
    m_btnCopyRaw->setStyleSheet(StyleHelper::getPastelBlueStyle());
    connect(m_btnCopyRaw, &QPushButton::clicked, this, &DebugRunnerWindow::onCopyRawClicked);

    m_btnClearOutput = new QPushButton("🧹 Çıktıyı Temizle", grpOutput);
    m_btnClearOutput->setStyleSheet(StyleHelper::getPastelSlateStyle());
    connect(m_btnClearOutput, &QPushButton::clicked, this, &DebugRunnerWindow::onClearOutputClicked);

    hOutBar->addWidget(m_lblResponseStatus);
    hOutBar->addWidget(m_lblResponseLatency);
    hOutBar->addStretch();
    hOutBar->addWidget(m_btnCopyRaw);
    hOutBar->addWidget(m_btnClearOutput);
    vOut->addLayout(hOutBar);

    m_txtRawOutput = new QTextEdit(grpOutput);
    m_txtRawOutput->setReadOnly(true);
    m_txtRawOutput->setStyleSheet("background-color: #0D0E15; border: 1px solid #313244; color: #50FA7B; "
                                 "font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; line-height: 1.4;");
    m_txtRawOutput->setPlaceholderText("Cihazdan dönen ham JSON veya HTTP yanıtı burada görüntülenecektir...");
    vOut->addWidget(m_txtRawOutput);

    vRight->addWidget(grpOutput);
    splitter->addWidget(wRight);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    // =========================================================================
    // 3. Bottom Log Console
    // =========================================================================
    m_txtLogs = new QTextEdit(central);
    m_txtLogs->setReadOnly(true);
    m_txtLogs->setFixedHeight(95);
    m_txtLogs->setStyleSheet("background-color: #11111B; border: 1px solid #313244; color: #A6ADC8; "
                            "font-family: 'Consolas', monospace; font-size: 11px;");
    mainLayout->addWidget(m_txtLogs);
}

void DebugRunnerWindow::appendLog(const QString &msg, const QString &color) {
    if (!m_txtLogs) return;
    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_txtLogs->append(QString("<span style='color:#6C7086;'>[%1]</span> <span style='color:%2;'>%3</span>")
                          .arg(timeStr, color, msg.toHtmlEscaped()));
}

void DebugRunnerWindow::updateSessionBadge(int sessionKey) {
    if (!m_lblSessionBadge) return;
    if (sessionKey > 0) {
        m_lblSessionBadge->setText(QString("🔑 SessionKey: %1").arg(sessionKey));
        m_lblSessionBadge->setStyleSheet("background-color: #064E3B; border: 1px solid #10B981; color: #34D399; padding: 4px 10px; border-radius: 4px; font-weight: bold;");
    } else {
        m_lblSessionBadge->setText("🔑 Oturum: Yok");
        m_lblSessionBadge->setStyleSheet("background-color: #450A0A; border: 1px solid #EF4444; color: #F87171; padding: 4px 10px; border-radius: 4px; font-weight: bold;");
    }
}

void DebugRunnerWindow::syncEndpointSettings() {
    if (!m_webClient) return;
    QString host = m_txtIp ? m_txtIp->text().trimmed() : "192.168.0.167";
    int port = m_spinPort ? m_spinPort->value() : 80;
    QString user = m_txtUser ? m_txtUser->text().trimmed() : "admin";
    QString pass = m_txtPass ? m_txtPass->text() : "1234";

    m_webClient->setHost(host, port);
    m_webClient->setCredentials(user, pass);
}

void DebugRunnerWindow::setPresetPayloadForAction(const QString &action) {
    if (!m_txtCustomPayload) return;
    int sk = m_webClient ? m_webClient->sessionKey() : 0;
    QString skStr = (sk > 0) ? QString::number(sk) : "{sessionKey}";

    if (action == "alarmStatus") {
        m_txtCustomPayload->setPlainText(QString("query=alarmStatus\nsessionKey=%1\n").arg(skStr));
    } else if (action == "deviceInfo") {
        m_txtCustomPayload->setPlainText(QString("query=deviceInfo\nsessionKey=%1\n").arg(skStr));
    } else if (action == "freq") {
        m_txtCustomPayload->setPlainText(QString("ResponseTo=freq\nsessionKey=%1\nfr_frequency=11054000\nfr_srate=30000000\nfr_pol=0\n").arg(skStr));
    } else if (action == "login") {
        QString user = m_txtUser ? m_txtUser->text().trimmed() : "admin";
        QString pass = m_txtPass ? m_txtPass->text() : "1234";
        m_txtCustomPayload->setPlainText(QString("ResponseTo=login\nsessionKey=\nlg_username=%1\nlg_password=%2\n").arg(user, pass));
    } else {
        m_txtCustomPayload->setPlainText(QString("query=alarmStatus\nsessionKey=%1\n").arg(skStr));
    }
}

void DebugRunnerWindow::onConnectClicked() {
    syncEndpointSettings();
    appendLog(QString("Cihaza bağlanılıyor ve giriş yapılıyor: %1@%2:%3").arg(m_webClient->username(), m_webClient->host()).arg(m_webClient->port()), "#89DCEB");
    m_webClient->login();
}

void DebugRunnerWindow::onPingClicked() {
    syncEndpointSettings();
    appendLog(QString("Host ping ediliyor: %1...").arg(m_webClient->host()), "#89DCEB");
    m_webClient->pingHost(1000);
}

void DebugRunnerWindow::onTuneClicked() {
    syncEndpointSettings();
    int freq = m_spinFreq->value();
    int sr = m_spinSymbolRate->value();
    QString pol = (m_comboPol->currentIndex() == 1) ? "H" : "V";

    appendLog(QString("Frekans gönderiliyor: %1 MHz, %2 kS/s, %3").arg(freq).arg(sr).arg(pol), "#FAB387");
    m_webClient->sendTune(freq, sr, pol);
}

void DebugRunnerWindow::onPollAlarmStatusClicked() {
    syncEndpointSettings();
    appendLog("alarmStatus telemetri sorgusu gönderiliyor...", "#89DCEB");
    m_webClient->requestStatus();
}

void DebugRunnerWindow::onPollDeviceInfoClicked() {
    syncEndpointSettings();
    appendLog("deviceInfo cihaz bilgisi sorgusu gönderiliyor...", "#89DCEB");
    m_webClient->requestDeviceInfo();
}

void DebugRunnerWindow::onSendCustomPostClicked() {
    syncEndpointSettings();
    QString raw = m_txtCustomPayload->toPlainText();
    int sk = m_webClient->sessionKey();
    if (sk > 0 && raw.contains("{sessionKey}")) {
        raw.replace("{sessionKey}", QString::number(sk));
        m_txtCustomPayload->setPlainText(raw);
    }
    appendLog("Özel CGI POST payload gönderiliyor...", "#CBA6F7");
    m_webClient->sendRaw(raw);
}

void DebugRunnerWindow::onStartSamplingClicked() {
    if (m_isSampling) {
        m_sampleTickTimer.stop();
        m_sampleDurationTimer.stop();
        m_isSampling = false;
        m_btnStartSample->setText("⏱️ Örnekleme Başlat");
        m_btnStartSample->setStyleSheet(StyleHelper::getPastelAmberStyle());
        appendLog("Örnekleme kullanıcı tarafından durduruldu.", "#FAB387");
        return;
    }

    m_powSamples.clear();
    m_cnSamples.clear();
    m_isSampling = true;
    m_btnStartSample->setText("⏹️ Durdur");
    m_btnStartSample->setStyleSheet(StyleHelper::getPastelRedStyle());

    int durationSec = m_spinSampleSec->value();
    m_sampleTickTimer.start(500);
    m_sampleDurationTimer.start(durationSec * 1000);
    appendLog(QString("⏱️ %1 saniyelik çoklu telemetri örnekleme başlatıldı (500 ms periyot)...").arg(durationSec), "#89DCEB");
}

void DebugRunnerWindow::onSampleTimerTick() {
    m_webClient->requestStatus();
}

void DebugRunnerWindow::onSamplingFinished() {
    m_sampleTickTimer.stop();
    m_isSampling = false;
    m_btnStartSample->setText("⏱️ Örnekleme Başlat");
    m_btnStartSample->setStyleSheet(StyleHelper::getPastelAmberStyle());

    if (!m_cnSamples.isEmpty() && !m_powSamples.isEmpty()) {
        double avgCn = std::accumulate(m_cnSamples.begin(), m_cnSamples.end(), 0.0) / m_cnSamples.size();
        double avgPow = std::accumulate(m_powSamples.begin(), m_powSamples.end(), 0.0) / m_powSamples.size();
        appendLog(QString("✔ Örnekleme tamamlandı (%1 örnek): Ort. C/N = %2 dB | Ort. Güç = %3 dBm")
                      .arg(m_cnSamples.size())
                      .arg(avgCn, 0, 'f', 2)
                      .arg(avgPow, 0, 'f', 2), "#34D399");
    } else {
        appendLog("Örnekleme tamamlandı ancak veri alınamadı.", "#EF4444");
    }
}

void DebugRunnerWindow::onSessionKeyReceived(int sessionKey) {
    updateSessionBadge(sessionKey);
    if (m_topClockBadge) {
        m_topClockBadge->setConnected(sessionKey > 0);
    }
    appendLog(QString("✔ Cihaz girişi başarılı! Oturum anahtarı: %1").arg(sessionKey), "#34D399");

    if (m_txtCustomPayload) {
        QString cur = m_txtCustomPayload->toPlainText();
        if (cur.contains("{sessionKey}")) {
            cur.replace("{sessionKey}", QString::number(sessionKey));
            m_txtCustomPayload->setPlainText(cur);
        } else if (cur.contains("sessionKey=\n")) {
            cur.replace("sessionKey=\n", QString("sessionKey=%1\n").arg(sessionKey));
            m_txtCustomPayload->setPlainText(cur);
        } else if (cur.contains("sessionKey=\r\n")) {
            cur.replace("sessionKey=\r\n", QString("sessionKey=%1\r\n").arg(sessionKey));
            m_txtCustomPayload->setPlainText(cur);
        }
    }
}

void DebugRunnerWindow::onLoginFailed(const QString &reason) {
    updateSessionBadge(-1);
    if (m_topClockBadge) {
        m_topClockBadge->setConnected(false);
    }
    appendLog(QString("❌ Cihaz girişi başarısız: %1").arg(reason), "#EF4444");
}

void DebugRunnerWindow::onTuneFinished(bool isOk, const QString &details) {
    if (isOk) {
        appendLog(QString("✔ Frekans başarıyla kilitlendi: %1").arg(details), "#34D399");
    } else {
        appendLog(QString("❌ Frekans ayarlama hatası: %1").arg(details), "#EF4444");
    }
}

void DebugRunnerWindow::onRawResponseReceived(const QString &actionName, const QByteArray &rawJson) {
    bool isErr = rawJson.contains("\"error\"") || rawJson.contains("denied") || rawJson.contains("failed") || rawJson.contains("timed out");
    if (!isErr) {
        m_lblResponseStatus->setText(QString("🟢 HTTP 200 OK (%1)").arg(actionName));
        m_lblResponseStatus->setStyleSheet("background-color: #064E3B; border: 1px solid #10B981; color: #34D399; padding: 4px 8px; border-radius: 4px; font-weight: bold;");
    } else {
        m_lblResponseStatus->setText(QString("🔴 Yanıt Hatası (%1)").arg(actionName));
        m_lblResponseStatus->setStyleSheet("background-color: #450A0A; border: 1px solid #EF4444; color: #F87171; padding: 4px 8px; border-radius: 4px; font-weight: bold;");
    }

    // Pretty format if JSON
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(rawJson, &parseErr);
    if (parseErr.error == QJsonParseError::NoError) {
        m_txtRawOutput->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
    } else {
        m_txtRawOutput->setPlainText(QString::fromUtf8(rawJson));
    }
}

void DebugRunnerWindow::onTelemetryReceived(double powerDbm, double cnDb, double ber, double ebN0Db) {
    if (m_isSampling) {
        m_powSamples.append(powerDbm);
        m_cnSamples.append(cnDb);
    }

    m_lblPower->setText(QString("Güç: %1 dBm").arg(powerDbm, 0, 'f', 2));
    m_lblCn->setText(QString("C/N: %1 dB").arg(cnDb, 0, 'f', 2));
    m_lblBer->setText(QString("BER: %1").arg(ber, 0, 'e', 2));
    m_lblEbN0->setText(QString("Eb/N0: %1 dB").arg(ebN0Db, 0, 'f', 2));

    if (cnDb > 10.0) {
        m_lblCn->setStyleSheet("background-color: #064E3B; color: #34D399; padding: 6px; border-radius: 4px; font-weight: bold;");
    } else {
        m_lblCn->setStyleSheet("background-color: #181825; color: #CDD6F4; padding: 6px; border-radius: 4px; font-weight: bold;");
    }
}

void DebugRunnerWindow::onStatusReceived(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName) {
    auto setLockBadge = [](QLabel *lbl, bool locked, const QString &name, const QString &token) {
        if (locked) {
            lbl->setText(QString("%1: KİLİTLİ (%2:+)").arg(name, token));
            lbl->setStyleSheet("background-color: #064E3B; border: 1px solid #10B981; color: #34D399; padding: 6px; border-radius: 4px; font-weight: bold;");
        } else {
            lbl->setText(QString("%1: KİLİTSİZ (%2:-)").arg(name, token));
            lbl->setStyleSheet("background-color: #450A0A; border: 1px solid #EF4444; color: #F87171; padding: 6px; border-radius: 4px; font-weight: bold;");
        }
    };

    setLockBadge(m_lblCarrier, signalLocked, "Taşıyıcı", "S");
    setLockBadge(m_lblVideo, videoLocked, "Video", "V");
    setLockBadge(m_lblAudio, audioLocked, "Audio", "A");

    if (!channelName.isEmpty()) {
        m_lblChannelName->setText("Kanal: " + channelName);
        m_lblChannelName->setStyleSheet("background-color: #1E1E2E; color: #89DCEB; padding: 6px; border-radius: 4px; font-weight: bold;");
    }
}

void DebugRunnerWindow::onTimestampReceived(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr) {
    if (m_topClockBadge) {
        m_topClockBadge->updateTime(deviceTime, hostTime, rawTimeStr);
    }
    if (m_lblDeviceTime) {
        m_lblDeviceTime->setText(QString("Cihaz Saati: %1").arg(rawTimeStr.isEmpty() ? "--" : rawTimeStr));
    }
    if (m_lblDeltaTime) {
        if (deviceTime.isValid()) {
            qint64 dtMs = hostTime.toMSecsSinceEpoch() - deviceTime.toMSecsSinceEpoch();
            m_lblDeltaTime->setText(QString("Δt (Gecikme): %1 ms").arg(dtMs));
            m_lblDeltaTime->setStyleSheet("background-color: #064E3B; color: #34D399; padding: 6px; border-radius: 4px; font-weight: bold;");
        } else {
            m_lblDeltaTime->setText("Δt (Gecikme): -- ms");
            m_lblDeltaTime->setStyleSheet("background-color: #181825; color: #CDD6F4; padding: 6px; border-radius: 4px; font-weight: bold;");
        }
    }
}

void DebugRunnerWindow::onCopyRawClicked() {
    if (!m_txtRawOutput) return;
    QClipboard *cb = QApplication::clipboard();
    cb->setText(m_txtRawOutput->toPlainText());
    appendLog("📋 Yanıt JSON panoya kopyalandı.", "#89DCEB");
}

void DebugRunnerWindow::onClearOutputClicked() {
    if (m_txtRawOutput) m_txtRawOutput->clear();
    m_lblResponseStatus->setText("HTTP Durum: Temizlendi");
    m_lblResponseStatus->setStyleSheet("background-color: #313244; color: #CDD6F4; padding: 4px 8px; border-radius: 4px;");
}

void DebugRunnerWindow::onOpenDbToolClicked() {
    auto *dbTool = new DbDebugWindow(this);
    dbTool->setAttribute(Qt::WA_DeleteOnClose);
    dbTool->show();
}
