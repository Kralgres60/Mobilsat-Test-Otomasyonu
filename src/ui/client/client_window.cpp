#include "client_window.h"
#include "appinfo.h"
#include "app_config.h"
#include "build_config.h"
#include "database/db_manager.h"
#include "ui/common/style_helper.h"
#include "ui/common/about_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QSplitter>

ClientWindow::ClientWindow(QWidget *parent)
    : QMainWindow(parent), m_beaconListener(AppConfig::instance().udpBeaconPort()) {
    
#if defined(NETA_ALGORITMA_1_FOUR_PC)
    QString algoTitle = "[ALGORİTMA 1: 4 PC (Kilitli Kuyruk)]";
#elif defined(NETA_ALGORITMA_2_THREE_PC)
    QString algoTitle = "[ALGORİTMA 2: 3 PC (Veritabanı Kuyruk)]";
#else
    QString algoTitle = "";
#endif

    setWindowTitle(QString("%1 - İstasyon %2 %3 v%4")
                       .arg(NETA_APP_TITLE)
                       .arg(AppConfig::instance().stationId())
                       .arg(algoTitle)
                       .arg(NETA_APP_VERSION));
    setMinimumSize(1280, 800);
    setWindowState(Qt::WindowMaximized);

    m_stateMachine = new TestStateMachine(&m_dutSerial, &m_netClient, this);

    // Web CGI İstemcilerinin Yapılandırılması
    m_dutWebClient.setHost(AppConfig::instance().stationDrxHost(), AppConfig::instance().stationDrxPort());
    m_dutWebClient.setCredentials(AppConfig::instance().drxUsername(), AppConfig::instance().drxPassword());
    connect(&m_dutWebClient, &DrxWebClient::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog((isErr ? "[DRX WEB HATA] " : "[DRX WEB] ") + msg);
    });

    m_refWebClient.setHost(AppConfig::instance().refDrxHost(), AppConfig::instance().refDrxPort());
    m_refWebClient.setCredentials(AppConfig::instance().drxUsername(), AppConfig::instance().drxPassword());
    connect(&m_refWebClient, &DrxWebClient::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog((isErr ? "[REF WEB HATA] " : "[REF WEB] ") + msg);
    });

    m_stateMachine->setWebClients(&m_dutWebClient, &m_refWebClient);

    setupUi();
    setupPlot();

    // Sinyal Bağlantıları
    connect(&m_beaconListener, &UdpBeaconListener::hostDiscovered, this, &ClientWindow::onHostDiscovered);
    connect(&m_netClient, &TcpClient::connected, this, &ClientWindow::onTcpConnected);
    connect(&m_netClient, &TcpClient::disconnected, this, &ClientWindow::onTcpDisconnected);
    connect(&m_netClient, &TcpClient::logMessage, this, &ClientWindow::appendLog);

    connect(&m_dutSerial, &SerialWorker::portOpened, this, [this](const QString &p) {
        m_btnConnectSerial->setText("Bağlantıyı Kes");
        m_btnConnectSerial->setStyleSheet("background-color: #C62828;");
        appendLog("Seri port bağlandı: " + p);
    });
    connect(&m_dutSerial, &SerialWorker::portClosed, this, [this]() {
        m_btnConnectSerial->setText("Bağlan");
        m_btnConnectSerial->setStyleSheet("background-color: #1976D2;");
        appendLog("Seri port kapatıldı.");
    });
    connect(&m_dutSerial, &SerialWorker::deviceReadyStateChanged, this, [this](DeviceReadyState state, const QString &/*desc*/) {
        if (!m_lblDeviceStatus) return;
        if (state == DeviceReadyState::Disconnected) {
            m_lblDeviceStatus->setText("🔴 DRX: Kapalı");
            m_lblDeviceStatus->setStyleSheet("background-color: #374151; color: #EF5350; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
        } else if (state == DeviceReadyState::PortOpenWaitingDevice) {
            m_lblDeviceStatus->setText("⚪ DRX: Bekleniyor (Açınız)");
            m_lblDeviceStatus->setStyleSheet("background-color: #263238; color: #90CAF9; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
        } else if (state == DeviceReadyState::Booting) {
            m_lblDeviceStatus->setText("🟡 DRX: Açılıyor (Boot)...");
            m_lblDeviceStatus->setStyleSheet("background-color: #E65100; color: #FFF; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
        } else if (state == DeviceReadyState::Ready) {
            m_lblDeviceStatus->setText("🟢 DRX: Hazır");
            m_lblDeviceStatus->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
        }
    });
    connect(&m_dutSerial, &SerialWorker::logMessage, this, &ClientWindow::appendLog);

    connect(m_stateMachine, &TestStateMachine::stateChanged, this, &ClientWindow::onStateChanged);
    connect(m_stateMachine, &TestStateMachine::channelStarted, this, &ClientWindow::onChannelStarted);
    connect(m_stateMachine, &TestStateMachine::channelSampleUpdated, this, &ClientWindow::onChannelSampleUpdated);
    connect(m_stateMachine, &TestStateMachine::channelCompleted, this, &ClientWindow::onChannelCompleted);
    connect(m_stateMachine, &TestStateMachine::testFinished, this, &ClientWindow::onTestFinished);
    connect(m_stateMachine, &TestStateMachine::waitingForLockUpdate, this, &ClientWindow::onWaitingForLockUpdate);
    connect(m_stateMachine, &TestStateMachine::logMessage, this, &ClientWindow::appendLog);

    // UDP Keşfini başlat
    m_beaconListener.start();
    appendLog("Lobi keşfi dinleniyor (Port " + QString::number(AppConfig::instance().udpBeaconPort()) + ")...");

    // Otomatik kayıtlı IP'ye bağlanmayı dene
    if (!AppConfig::instance().hostIp().isEmpty()) {
        m_netClient.connectToHost(AppConfig::instance().hostIp(), AppConfig::instance().tcpPort());
    }

    // Veritabanı ve siparişleri yükle
    loadWorkOrdersAndProfiles();
    onRefreshPortsClicked();
}

ClientWindow::~ClientWindow() {
    m_beaconListener.stop();
    m_dutSerial.closePort();
}

void ClientWindow::setupUi() {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(10, 8, 10, 6);

    // --- 1. KOMPAKT ÜST KONTROL ŞERİDİ (2 HİZALI SATIR) ---
    QFrame *topControlFrame = new QFrame(this);
    topControlFrame->setStyleSheet("background-color: #1A1F26; border-radius: 6px; padding: 4px;");
    QVBoxLayout *topVBox = new QVBoxLayout(topControlFrame);
    topVBox->setSpacing(4);
    topVBox->setContentsMargins(6, 4, 6, 4);

    // Satır 1: Başlık, Durum Rozetleri ve Port Bağlantısı
    QHBoxLayout *topRow1 = new QHBoxLayout();
    QLabel *lblTitle = new QLabel(QString("🏭 <b>NETA DRX9000 İSTASYON %1</b>").arg(AppConfig::instance().stationId()), this);
    lblTitle->setStyleSheet("font-size: 14px; color: #64B5F6;");

    m_lblConnectionStatus = new QLabel("🔴 Sunucu Aranıyor...", this);
    m_lblConnectionStatus->setStyleSheet("background-color: #374151; color: #D1D5DB; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");

    m_lblLockBanner = new QLabel("⚪ Sunucu Bekleniyor", this);
    m_lblLockBanner->setStyleSheet("background-color: #374151; color: #9CA3AF; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");

    QLabel *lblPort = new QLabel("DRX Port:", this);
    lblPort->setStyleSheet("color: #9CA3AF; font-size: 12px;");
    m_comboSerialPorts = new QComboBox(this);
    m_comboSerialPorts->setMinimumWidth(85);

    QPushButton *btnRefresh = new QPushButton("🔄", this);
    btnRefresh->setFixedSize(26, 26);
    connect(btnRefresh, &QPushButton::clicked, this, &ClientWindow::onRefreshPortsClicked);

    m_btnConnectSerial = new QPushButton("Bağlan", this);
    m_btnConnectSerial->setFixedHeight(26);
    connect(m_btnConnectSerial, &QPushButton::clicked, this, &ClientWindow::onConnectSerialClicked);

    m_lblDeviceStatus = new QLabel("🔴 DRX: Kapalı", this);
    m_lblDeviceStatus->setStyleSheet("background-color: #374151; color: #EF5350; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");

    topRow1->addWidget(lblTitle);
    topRow1->addSpacing(10);
    topRow1->addWidget(m_lblConnectionStatus);
    topRow1->addWidget(m_lblLockBanner);
    topRow1->addStretch();
    topRow1->addWidget(lblPort);
    topRow1->addWidget(m_comboSerialPorts);
    topRow1->addWidget(btnRefresh);
    topRow1->addWidget(m_btnConnectSerial);
    topRow1->addWidget(m_lblDeviceStatus);

    QPushButton *btnAbout = new QPushButton("ℹ️ Hakkında", this);
    btnAbout->setFixedHeight(24);
    btnAbout->setCursor(Qt::PointingHandCursor);
    btnAbout->setStyleSheet(
        "QPushButton { background-color: #1E2530; color: #90CAF9; border: 1px solid #37474F; border-radius: 4px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background-color: #263238; border-color: #64B5F6; color: #FFFFFF; }"
    );
    connect(btnAbout, &QPushButton::clicked, this, &ClientWindow::onAboutClicked);
    topRow1->addSpacing(8);
    topRow1->addWidget(btnAbout);

    topVBox->addLayout(topRow1);

    // Satır 2: İş Emri, Barkod Girişi ve Test Başlat/Durdur Butonları
    QHBoxLayout *topRow2 = new QHBoxLayout();
    QLabel *lblOrder = new QLabel("İş Emri:", this);
    lblOrder->setStyleSheet("color: #E0E6ED; font-weight: bold; font-size: 12px;");
    m_comboOrders = new QComboBox(this);
    m_comboOrders->setMinimumWidth(220);

    QLabel *lblSn = new QLabel("Barkod / Seri No:", this);
    lblSn->setStyleSheet("color: #E0E6ED; font-weight: bold; font-size: 12px;");
    m_txtAntennaSn = new QLineEdit(this);
    m_txtAntennaSn->setPlaceholderText("Anten barkodunu okutunuz...");
    m_txtAntennaSn->setMinimumWidth(220);
    m_txtAntennaSn->setStyleSheet("font-size: 14px; font-weight: bold; color: #FFF; background-color: #263238; border: 1px solid #37474F; padding: 4px 8px;");
    connect(m_txtAntennaSn, &QLineEdit::returnPressed, this, &ClientWindow::onStartTestClicked);

    m_btnStartTest = new QPushButton("▶  TESTİ BAŞLAT", this);
    m_btnStartTest->setStyleSheet("background-color: #2E7D32; font-size: 13px; font-weight: bold; padding: 6px 20px; border-radius: 4px;");
    connect(m_btnStartTest, &QPushButton::clicked, this, &ClientWindow::onStartTestClicked);

    m_btnAbortTest = new QPushButton("⏹  DURDUR", this);
    m_btnAbortTest->setStyleSheet("background-color: #B71C1C; font-size: 13px; font-weight: bold; padding: 6px 14px; border-radius: 4px;");
    m_btnAbortTest->setEnabled(false);
    connect(m_btnAbortTest, &QPushButton::clicked, this, &ClientWindow::onAbortTestClicked);

    topRow2->addWidget(lblOrder);
    topRow2->addWidget(m_comboOrders);
    topRow2->addSpacing(10);
    topRow2->addWidget(lblSn);
    topRow2->addWidget(m_txtAntennaSn);
    topRow2->addSpacing(10);
    topRow2->addWidget(m_btnStartTest);
    topRow2->addWidget(m_btnAbortTest);
    topRow2->addStretch();
    topVBox->addLayout(topRow2);

    mainLayout->addWidget(topControlFrame);

    // --- 2. ORTA BÖLÜM: 10 KANAL MATRİSİ VE SAĞDA GRAFİK / SONUÇ PANELİ ---
    QHBoxLayout *midLayout = new QHBoxLayout();
    midLayout->setSpacing(8);

    // Sol Taraf: 10 Kanal Matrisi (2 satır x 5 sütun)
    QGroupBox *grpChannels = new QGroupBox("10 Kanal Test Matrisi", this);
    grpChannels->setStyleSheet("margin-top: 10px; padding: 6px;");
    QGridLayout *gridChannels = new QGridLayout(grpChannels);
    gridChannels->setSpacing(6);
    gridChannels->setContentsMargins(4, 8, 4, 4);

    for (int i = 0; i < 10; ++i) {
        ChannelCardWidget *card = new ChannelCardWidget(i + 1, grpChannels);
        m_channelCards.append(card);
        int row = i / 5;
        int col = i % 5;
        gridChannels->addWidget(card, row, col);
    }
    midLayout->addWidget(grpChannels, 62);

    // Sağ Taraf: Sonuç Banner'ı ve Canlı Spektrum Grafiği
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(6);

    QFrame *resultCard = new QFrame(this);
    resultCard->setStyleSheet("background-color: #1A1F26; border: 2px solid #374151; border-radius: 6px; padding: 6px;");
    QVBoxLayout *resVBox = new QVBoxLayout(resultCard);
    resVBox->setSpacing(2);
    resVBox->setContentsMargins(6, 6, 6, 6);

    m_lblOverallResult = new QLabel("BEKLEMEDE", resultCard);
    m_lblOverallResult->setAlignment(Qt::AlignCenter);
    m_lblOverallResult->setStyleSheet("font-size: 24px; font-weight: bold; color: #9E9E9E;");

    m_lblOverallDetails = new QLabel("Hazır: Barkod okutunuz", resultCard);
    m_lblOverallDetails->setAlignment(Qt::AlignCenter);
    m_lblOverallDetails->setStyleSheet("font-size: 12px; color: #B0BEC5;");

    resVBox->addWidget(m_lblOverallResult);
    resVBox->addWidget(m_lblOverallDetails);
    rightLayout->addWidget(resultCard, 20);

    m_plot = new QCustomPlot(this);
    rightLayout->addWidget(m_plot, 80);

    midLayout->addLayout(rightLayout, 38);
    mainLayout->addLayout(midLayout, 1);

    // --- 3. İSTEĞE BAĞLI / KATLANABİLİR LOG PENCERESİ (VARSAYILAN GİZLİ) ---
    m_logDrawer = new QFrame(this);
    m_logDrawer->setStyleSheet("background-color: #121418; border: 1px solid #2B3240; border-radius: 4px;");
    QVBoxLayout *logVBox = new QVBoxLayout(m_logDrawer);
    logVBox->setContentsMargins(4, 4, 4, 4);
    logVBox->setSpacing(2);

    m_txtLog = new QTextEdit(m_logDrawer);
    m_txtLog->setReadOnly(true);
    m_txtLog->setFixedHeight(90);
    m_txtLog->setStyleSheet("background-color: #0E1116; color: #81C784; font-family: Consolas, monospace; font-size: 11px;");
    logVBox->addWidget(m_txtLog);
    m_logDrawer->setVisible(false); // Başlangıçta gizli!
    mainLayout->addWidget(m_logDrawer);

    // --- 4. EN ALT DURUM ÇUBUĞU (STATUS BAR) ---
    QHBoxLayout *botStatusLayout = new QHBoxLayout();
    botStatusLayout->setContentsMargins(4, 2, 4, 2);

    QLabel *lblHelpHint = new QLabel("İpucu: Barkod okuttuktan sonra Enter'a basarak testi doğrudan başlatabilirsiniz.", this);
    lblHelpHint->setStyleSheet("color: #78909C; font-size: 11px;");

    m_btnToggleLog = new QPushButton("📋 Konsol Logu (Göster)", this);
    m_btnToggleLog->setFixedSize(150, 22);
    m_btnToggleLog->setStyleSheet("background-color: #242B35; color: #90CAF9; font-size: 11px; border: 1px solid #374151; padding: 2px;");
    connect(m_btnToggleLog, &QPushButton::clicked, this, &ClientWindow::onToggleLogClicked);

    botStatusLayout->addWidget(lblHelpHint);
    botStatusLayout->addStretch();

    QLabel *lblDev = new QLabel(QString("Geliştirici: <b>%1</b> | %2").arg(NETA_APP_DEVELOPER, NETA_APP_ORGANIZATION), this);
    lblDev->setStyleSheet("color: #78909C; font-size: 11px;");
    botStatusLayout->addWidget(lblDev);
    botStatusLayout->addSpacing(16);

    botStatusLayout->addWidget(m_btnToggleLog);
    mainLayout->addLayout(botStatusLayout);
}

void ClientWindow::setupPlot() {
    m_plot->setBackground(QBrush(QColor(26, 31, 38)));
    m_plot->xAxis->setBasePen(QPen(QColor(90, 105, 120)));
    m_plot->yAxis->setBasePen(QPen(QPen(QColor(90, 105, 120))));
    m_plot->xAxis->setTickPen(QPen(QColor(90, 105, 120)));
    m_plot->yAxis->setTickPen(QPen(QColor(90, 105, 120)));
    m_plot->xAxis->setTickLabelColor(QColor(160, 175, 190));
    m_plot->yAxis->setTickLabelColor(QColor(160, 175, 190));
    m_plot->xAxis->setLabel("Örnek");
    m_plot->yAxis->setLabel("C/N (dB)");
    m_plot->xAxis->setLabelColor(QColor(160, 175, 190));
    m_plot->yAxis->setLabelColor(QColor(160, 175, 190));

    // Grafik 0: DUT C/N (Mavi Çizgi)
    m_plot->addGraph();
    m_plot->graph(0)->setPen(QPen(QColor(33, 150, 243), 2));
    m_plot->graph(0)->setName("DUT C/N");

    // Grafik 1: REF C/N (Sarı/Turuncu Çizgi)
    m_plot->addGraph();
    m_plot->graph(1)->setPen(QPen(QColor(255, 179, 0), 2));
    m_plot->graph(1)->setName("REF C/N");

    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(36, 43, 53, 200)));
    m_plot->legend->setTextColor(QColor(240, 240, 240));

    m_plot->xAxis->setRange(0, 30);
    m_plot->yAxis->setRange(0, 20);
    m_plot->replot();
}

void ClientWindow::loadWorkOrdersAndProfiles() {
    DbManager &db = DbManager::instance();
    if (!db.isConnected()) {
        db.connectToDatabase(AppConfig::instance().dbHost(), AppConfig::instance().dbPort(),
                             AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                             AppConfig::instance().dbPassword());
    }

    m_workOrders = db.getActiveWorkOrders();
    m_comboOrders->clear();
    for (const auto &wo : m_workOrders) {
        m_comboOrders->addItem(QString("%1 - %2 (%3)").arg(wo.orderNo, wo.customerName, wo.productCode), wo.id);
    }

    auto profiles = db.getProfiles();
    if (!profiles.isEmpty()) {
        m_currentProfile = profiles.first();
        for (int i = 0; i < m_channelCards.size(); ++i) {
            if (i < m_currentProfile.channels.size()) {
                m_channelCards[i]->setConfig(m_currentProfile.channels[i]);
            }
        }
    }
}

void ClientWindow::onRefreshPortsClicked() {
    m_comboSerialPorts->clear();
    QStringList ports = SerialWorker::availablePorts();
    m_comboSerialPorts->addItems(ports);

    int idx = m_comboSerialPorts->findText(AppConfig::instance().serialPort());
    if (idx >= 0) m_comboSerialPorts->setCurrentIndex(idx);
}

void ClientWindow::onConnectSerialClicked() {
    if (m_dutSerial.isConnected()) {
        m_dutSerial.closePort();
    } else {
        QString port = m_comboSerialPorts->currentText();
        if (port.isEmpty()) {
            QMessageBox::warning(this, "Uyarı", "Lütfen bir seri port seçiniz!");
            return;
        }
        AppConfig::instance().setSerialPort(port);
        AppConfig::instance().save();
        m_dutSerial.openPort(port, AppConfig::instance().serialBaudRate());
    }
}

void ClientWindow::onHostDiscovered(const QString &hostIp, const QString &serverName, quint16 tcpPort) {
    if (!m_netClient.isConnected()) {
        appendLog(QString("Lobi keşfedildi: %1 (%2:%3). Bağlanılıyor...").arg(serverName, hostIp).arg(tcpPort));
        AppConfig::instance().setHostIp(hostIp);
        AppConfig::instance().setDbHost(hostIp);
        m_netClient.connectToHost(hostIp, tcpPort);
    }
}

void ClientWindow::onTcpConnected() {
    m_lblConnectionStatus->setText(QString("🟢 Sunucuya Bağlı (%1)").arg(AppConfig::instance().hostIp()));
    m_lblConnectionStatus->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
    m_lblLockBanner->setText("🟢 Referans Müsait");
    m_lblLockBanner->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
    
    // Veritabanı henüz bağlı değilse veya iş emirleri boşsa sunucu üzerinden yeniden yükle
    if (!DbManager::instance().isConnected() || m_workOrders.isEmpty()) {
        AppConfig::instance().setDbHost(AppConfig::instance().hostIp());
        loadWorkOrdersAndProfiles();
    }

    m_netClient.registerStation(AppConfig::instance().stationId(), AppConfig::instance().stationName());
}

void ClientWindow::onTcpDisconnected() {
    m_lblConnectionStatus->setText("🔴 Sunucu Bağlantısı Koptu");
    m_lblConnectionStatus->setStyleSheet("background-color: #B71C1C; color: #FFCDD2; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
    m_lblLockBanner->setText("⚪ Sunucu Bekleniyor");
    m_lblLockBanner->setStyleSheet("background-color: #374151; color: #9CA3AF; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
}

void ClientWindow::onStartTestClicked() {
    QString sn = m_txtAntennaSn->text().trimmed();
    if (sn.isEmpty()) {
        QMessageBox::warning(this, "Uyarı", "Lütfen anten barkodunu okutunuz!");
        m_txtAntennaSn->setFocus();
        return;
    }

    if (!m_netClient.isConnected()) {
        QMessageBox::warning(this, "Yönetici Sunucu Bağlantısı Yok",
                             "Yönetici Sunucuya (Host PC) henüz bağlı değilsiniz!\n\n"
                             "• Çoklu istasyon fabrika testlerinde referans çanak anten senkronizasyonu ve kilit mekanizması için Yönetici programının açık olması gereklidir.\n"
                             "• Lütfen Yönetici Sunucu bilgisayarındaki uygulamayı başlatınız.");
        return;
    }

    if (!m_dutSerial.isConnected()) {
        QMessageBox::warning(this, "Uyarı", "Test cihazı DRX9000 seri portuna bağlı değil!");
        return;
    }

    if (!m_dutSerial.isDeviceReady()) {
        QMessageBox::warning(this, "Cihaz Bekleniyor", QString("Cihaz henüz tam hazır değil (%1).\nLütfen DRX9000 cihazının açılmasını bekleyiniz.")
                                                        .arg(m_dutSerial.readyStateString()));
        return;
    }

    int woId = m_comboOrders->currentData().toInt();

    // Kartları ve grafiği sıfırla
    for (auto *card : m_channelCards) {
        card->reset();
    }
    m_plot->graph(0)->data()->clear();
    m_plot->graph(1)->data()->clear();
    m_plot->replot();

    m_lblOverallResult->setText("TEST DEVAM EDİYOR");
    m_lblOverallResult->setStyleSheet("font-size: 24px; font-weight: bold; color: #2196F3;");
    m_lblOverallDetails->setText("Referans kilidi talep ediliyor...");

    m_btnStartTest->setEnabled(false);
    m_btnAbortTest->setEnabled(true);

    m_stateMachine->startTest(sn, m_currentProfile, woId);
}

void ClientWindow::onAbortTestClicked() {
    m_stateMachine->abortTest("Operatör tarafından durduruldu.");
}

void ClientWindow::onStateChanged(TestState /*state*/, const QString &stateName) {
    m_lblLockBanner->setText("Durum: " + stateName);
}

void ClientWindow::onChannelStarted(int channelNo, const QString &/*name*/, int /*freq*/, const QString &/*pol*/) {
    int idx = channelNo - 1;
    if (idx >= 0 && idx < m_channelCards.size()) {
        m_channelCards[idx]->setActive(true);
    }
}

void ClientWindow::onChannelSampleUpdated(int channelNo, double dutCn, double /*dutPow*/, double refCn, double /*refPow*/, double delta) {
    int idx = channelNo - 1;
    if (idx >= 0 && idx < m_channelCards.size()) {
        m_channelCards[idx]->updateSample(dutCn, 0, refCn, 0, delta);
    }

    // Grafiğe ekle
    int count = m_plot->graph(0)->dataCount();
    m_plot->graph(0)->addData(count, dutCn);
    m_plot->graph(1)->addData(count, refCn);
    m_plot->xAxis->setRange(qMax(0, count - 30), count + 5);
    m_plot->replot();
}

void ClientWindow::onChannelCompleted(const ChannelSampleResult &result) {
    int idx = result.channelNo - 1;
    if (idx >= 0 && idx < m_channelCards.size()) {
        m_channelCards[idx]->setResult(result);
    }
}

void ClientWindow::onWaitingForLockUpdate(int queuePos, int activeStation) {
    m_lblLockBanner->setText(QString("⏳ MEŞGUL (İstasyon %1 Testte) - Sıra: %2")
                                 .arg(activeStation).arg(queuePos));
    m_lblLockBanner->setStyleSheet("background-color: #E65100; color: #FFF; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");
}

void ClientWindow::onTestFinished(const TestRunData &finalResult) {
    m_btnStartTest->setEnabled(true);
    m_btnAbortTest->setEnabled(false);

    if (finalResult.overallResult == "PASS") {
        m_lblOverallResult->setText("✔ GEÇTİ (PASS)");
        m_lblOverallResult->setStyleSheet("font-size: 28px; font-weight: bold; color: #4CAF50;");
        m_lblOverallDetails->setText(QString("Süre: %1 sn | Ort. Delta: %2 dB | Max: %3 dB")
                                         .arg(finalResult.durationSec)
                                         .arg(finalResult.avgDeltaDb, 0, 'f', 2)
                                         .arg(finalResult.maxDeltaDb, 0, 'f', 2));
    } else {
        m_lblOverallResult->setText("✖ KALDI (FAIL)");
        m_lblOverallResult->setStyleSheet("font-size: 28px; font-weight: bold; color: #EF5350;");
        m_lblOverallDetails->setText(QString("Neden: %1").arg(finalResult.failReason));
    }

    m_lblLockBanner->setText("🟢 Referans Müsait");
    m_lblLockBanner->setStyleSheet("background-color: #1B5E20; color: #A5D6A7; border-radius: 10px; padding: 2px 10px; font-weight: bold; font-size: 11px;");

    // Veritabanına kaydet
    DbManager::instance().insertTestRun(finalResult);

    m_txtAntennaSn->clear();
    m_txtAntennaSn->setFocus();
}

void ClientWindow::onToggleLogClicked() {
    bool visible = !m_logDrawer->isVisible();
    m_logDrawer->setVisible(visible);
    m_btnToggleLog->setText(visible ? "📋 Konsol Logu (Gizle)" : "📋 Konsol Logu (Göster)");
}

void ClientWindow::appendLog(const QString &text) {
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_txtLog->append(QString("[%1] %2").arg(timeStr, text));
}

void ClientWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}
