/**
 * @file host_window.cpp
 * @brief Implementation of the NETA Green Central Host dashboard with sidebar navigation and Reference Ethernet Ping & Web CGI control.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "host_window.h"
#include "appinfo.h"
#include "app_config.h"
#include "build_config.h"
#include "core/app_logger.h"
#include "database/db_manager.h"
#include "core/dependency_installer.h"
#include "ui/common/style_helper.h"
#include "ui/common/about_dialog.h"
#include "ui/common/login_dialog.h"
#include "ui/common/lobby_window.h"
#include "ui/client/client_window.h"
#include "ui/debug/debug_runner_window.h"
#include "ui/debug/db_debug_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QScrollArea>
#include <QHeaderView>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QTcpSocket>
#include <QFile>
#include <QDir>
#include <QFileDialog>
#include <QDateTime>
#include <QPixmap>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>

HostWindow::HostWindow(QWidget *parent)
    : QMainWindow(parent),
      m_tcpServer(&m_arbiter, nullptr, this),
      m_beaconBroadcaster(AppConfig::instance().udpBeaconPort(), AppConfig::instance().tcpPort(), this) {

    setAttribute(Qt::WA_DeleteOnClose);

#if defined(NETA_ALGORITMA_1_FOUR_PC)
    QString algoTitle = "[4 PC - Yönetici Sunucu]";
#elif defined(NETA_ALGORITMA_2_THREE_PC)
    QString algoTitle = "[3 PC - İzleme Paneli]";
#else
    QString algoTitle = "";
#endif

    setWindowTitle(QString("%1 - Fabrika Yönetim Sunucusu (HOST) %2 v%3")
                       .arg(NETA_APP_TITLE)
                       .arg(algoTitle)
                       .arg(NETA_APP_VERSION));
    setMinimumSize(1180, 740);
    setWindowState(Qt::WindowMaximized);

    // Primary Reference CAT6 Ethernet Web CGI Client + RS232 fallback bridged via DataStream
    m_refWebClient.setHost(AppConfig::instance().refDrxHost(), AppConfig::instance().refDrxPort());
    m_refWebClient.setCredentials(AppConfig::instance().drxUsername(), AppConfig::instance().drxPassword());

    DrxTransportOps refOps;
    refOps.name = QStringLiteral("HostReferenceDRX");
    refOps.isConnectedFn = [this]() {
        return m_refWebClient.isReachable() || m_refSerial.isConnected();
    };
    refOps.isReadyFn = [this]() {
        return m_refWebClient.isReachable() || m_refSerial.isConnected();
    };
    refOps.tuneFn = [this](int freq, int sr, const QString &pol) {
        if (m_refWebClient.isReachable()) {
            if (!m_refWebClient.isLoggedIn()) {
                m_refWebClient.testConnectionAndSync();
            }
            m_refWebClient.sendTune(freq, sr, pol);
        }
        if (m_refSerial.isConnected()) {
            m_refSerial.sendTune(freq, sr, pol);
            m_refSerial.requestStatus();
        }
    };
    refOps.statusFn = [this]() {
        if (m_refWebClient.isReachable()) {
            m_refWebClient.requestStatus();
        }
        if (m_refSerial.isConnected()) {
            m_refSerial.requestStatus();
        }
    };
    m_refStream = new DataStream(refOps, this);
    connect(&m_refWebClient, &Ethernet::statusReceived, m_refStream, &DataStream::pushStatus);
    connect(&m_refWebClient, &Ethernet::telemetryReceived, m_refStream, &DataStream::pushTelemetry);
    connect(&m_refWebClient, &Ethernet::timestampReceived, m_refStream, &DataStream::pushTimestamp);
    connect(&m_refSerial, &RS232::statusReceived, m_refStream, &DataStream::pushStatus);
    connect(&m_refSerial, &RS232::telemetryReceived, m_refStream, &DataStream::pushTelemetry);
    m_tcpServer.setRefDataStream(m_refStream);

    setupMenuBar();
    setupUi();

    // Reference Ethernet Ping & Web CGI Signal Wiring
    connect(&m_refWebClient, &DrxWebClient::logMessage, this, [this](const QString &msg, bool isErr) {
        appendLog((isErr ? "[REF ETH HATA] " : "[REF ETH] ") + msg);
    });
    connect(&m_refWebClient, &DrxWebClient::pingFinished, this, &HostWindow::onRefPingFinished);
    connect(&m_refWebClient, &DrxWebClient::loginSucceeded, this, &HostWindow::onRefWebLoginSucceeded);
    connect(&m_refWebClient, &DrxWebClient::loginFailed, this, &HostWindow::onRefWebLoginFailed);
    connect(&m_refWebClient, &DrxWebClient::telemetryReceived, this, [this](double powDbm, double cnDb, double /*ber*/, double /*ebn0*/) {
        m_lblRefTelemetry->setText(QString("📡 Ref Canlı: C/N=%1 dB | POW=%2 dBm").arg(cnDb, 0, 'f', 1).arg(powDbm, 0, 'f', 1));
        m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelBlueStyle());
    });
    connect(&m_refWebClient, &DrxWebClient::timestampReceived, this,
            [this](const QDateTime &devTime, const QDateTime &hTime, const QString &rawStr) {
        if (m_topDrxClockBadge) {
            m_topDrxClockBadge->updateTime(devTime, hTime, rawStr);
        }
    });
    connect(&m_refWebClient, &DrxWebClient::loginSucceeded, this,
            [this](int) {
        if (m_topDrxClockBadge) {
            m_topDrxClockBadge->setConnected(true);
        }
    });
    connect(&m_refWebClient, &DrxWebClient::loginFailed, this,
            [this](const QString &) {
        if (m_topDrxClockBadge) {
            m_topDrxClockBadge->setConnected(false);
        }
    });

    // Network & Arbiter Signal Wiring
    connect(&m_tcpServer, &TcpServer::stationConnected, this, &HostWindow::onStationConnected);
    connect(&m_tcpServer, &TcpServer::stationDisconnected, this, &HostWindow::onStationDisconnected);
    connect(&m_tcpServer, &TcpServer::stationStatusUpdated, this, &HostWindow::onStationStatusUpdated);
    connect(&m_tcpServer, &TcpServer::logMessage, this, &HostWindow::appendLog);

    connect(&m_arbiter, &LockArbiter::lockGranted, this, &HostWindow::onLockGranted);
    connect(&m_arbiter, &LockArbiter::lockReleased, this, &HostWindow::onLockReleased);
    connect(&m_arbiter, &LockArbiter::lockExpired, this, &HostWindow::onLockExpired);

    // Optional Legacy Reference RS232 Fallback Wiring (Settings tab only)
    connect(&m_refSerial, &SerialWorker::portOpened, this, [this](const QString &p) {
        if (m_btnConnectRefSerial) {
            m_btnConnectRefSerial->setText("RS232 Kes");
            m_btnConnectRefSerial->setStyleSheet("background-color: #B91C1C; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
        }
        appendLog("[Ref RS232 Yedek] Seri port açıldı: " + p);
    });
    connect(&m_refSerial, &SerialWorker::portClosed, this, [this]() {
        if (m_btnConnectRefSerial) {
            m_btnConnectRefSerial->setText("RS232 Bağlan");
            m_btnConnectRefSerial->setStyleSheet("background-color: #0284C7; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
        }
        appendLog("[Ref RS232 Yedek] Seri port kapatıldı.");
    });
    connect(&m_refSerial, &SerialWorker::telemetryReceived, this, [this](double powDbm, double cnDb, double /*ber*/) {
        m_lblRefTelemetry->setText(QString("📡 Ref Canlı: C/N=%1 dB | POW=%2 dBm").arg(cnDb, 0, 'f', 1).arg(powDbm, 0, 'f', 1));
        m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelBlueStyle());
    });

    // Automatically pull all data whenever central MariaDB connects or reconnects later
    connect(&DbManager::instance(), &DbManager::connectionStatusChanged, this,
            [this](bool connected, const QString &) {
        const AppConfig &cfg = AppConfig::instance();
        if (m_lblDbStatus) {
            if (connected) {
                m_lblDbStatus->setText(QString("✔ Bağlı (%1:%2 / %3)")
                                           .arg(cfg.dbHost()).arg(cfg.dbPort()).arg(cfg.dbName()));
                m_lblDbStatus->setStyleSheet("color: #34D399; font-weight: bold;");
            } else {
                m_lblDbStatus->setText("❌ Bağlı Değil / Servis Bekleniyor");
                m_lblDbStatus->setStyleSheet("color: #F87171; font-weight: bold;");
            }
        }
        if (!connected) return;

        loadWorkOrders();
        const ReferenceMode activeMode = DbManager::instance().getReferenceMode();
        m_tcpServer.setReferenceMode(activeMode);
        updateRefModeBadgeUi(activeMode);
        if (m_goldenRefWidget) {
            m_goldenRefWidget->setActiveReferenceMode(activeMode);
            m_goldenRefWidget->reloadFrequencies();
        }
        if (m_historyWidget) {
            m_historyWidget->refreshHistory();
        }
        if (m_dbViewerWidget) {
            m_dbViewerWidget->refreshHistory();
        }
        if (m_userManager) {
            m_userManager->reloadUsers();
        }
        m_tcpServer.broadcastUserRoleUpdates();
    });

    // Background 8s Ping Heartbeat for Reference DRX9000, DB Retry & Cluster Node Heartbeat
    connect(&m_refEthPingTimer, &QTimer::timeout, this, [this]() {
        m_refWebClient.pingHost(1000);
        if (!DbManager::instance().isConnected()) {
            const AppConfig &cfg = AppConfig::instance();
            if (DbManager::instance().connectToDatabase(cfg.dbHost(), cfg.dbPort(),
                                                       cfg.dbName(), cfg.dbUser(),
                                                       cfg.dbPassword())) {
                appendLog(QString("[Veritabanı] Merkezi MariaDB'ye bağlanıldı ve veriler çekildi (%1:%2).")
                              .arg(cfg.dbHost()).arg(cfg.dbPort()));
            }
        }
        if (!DbManager::instance().isConnected()) return;
        DbManager::instance().upsertClusterNode(
            "HOST_SERVER",
            AppConfig::instance().serverName(),
            "127.0.0.1",
            AppConfig::instance().tcpPort(),
            "ONLINE",
            QString(),
            "Central Host Server");
    });
    m_refEthPingTimer.start(8000);

    // Periodic 2s idle telemetry polling so Host UI & Simulator stay live with C/N and POW
    connect(&m_refTelemetryPollTimer, &QTimer::timeout, this, [this]() {
        if (m_goldenRefWidget && m_goldenRefWidget->isCaptureActive()) {
            return;
        }
        if (m_refWebClient.isReachable() && m_refWebClient.isLoggedIn()) {
            m_refWebClient.requestStatus();
        } else if (m_refSerial.isConnected()) {
            m_refSerial.requestStatus();
        }
    });
    m_refTelemetryPollTimer.start(2000);

    // Start TCP Server & UDP Beacon
    m_tcpServer.start(AppConfig::instance().tcpPort());
    m_beaconBroadcaster.setServerName(AppConfig::instance().serverName());
    m_beaconBroadcaster.setStationCount(0);
    m_beaconBroadcaster.start(2000);
    appendLog(QString("Lobi yayını başladı (UDP %1, TCP %2).").arg(AppConfig::instance().udpBeaconPort()).arg(AppConfig::instance().tcpPort()));
    setStatusMessage(QString("Yönetici Sunucu Aktif (TCP %1 / UDP %2) | Referans Ethernet Ping kontrol ediliyor...")
                         .arg(AppConfig::instance().tcpPort()).arg(AppConfig::instance().udpBeaconPort()));

    onRefreshPortsClicked();
    if (DbManager::instance().isConnected()) {
        DbManager::instance().ensureDefaultDataIfMissing();
        DbManager::instance().upsertClusterNode(
            "HOST_SERVER",
            AppConfig::instance().serverName(),
            "127.0.0.1",
            AppConfig::instance().tcpPort(),
            "ONLINE",
            QString(),
            "Central Host Server");
    }

    // Auto-backup service initialization & signal connections
    BackupManager::instance().startAutoBackup(24);
    connect(&BackupManager::instance(), &BackupManager::backupCompleted, this,
            [this](const QString &path, qint64 size) {
        QString filename = QFileInfo(path).fileName();
        if (m_lblLastBackupInfo) {
            m_lblLastBackupInfo->setText(QString("Son Yedek: %1 (%2 KB) | Otomatik: Aktif (24s)")
                .arg(filename)
                .arg(size / 1024));
        }
        appendLog(QString("💾 [Otomatik Yedek] Başarılı: %1 (%2 KB)").arg(filename).arg(size / 1024));
    });
    connect(&BackupManager::instance(), &BackupManager::backupFailed, this,
            [this](const QString &err) {
        appendLog(QString("❌ [Yedekleme Hatası] %1").arg(err));
    });
    connect(&BackupManager::instance(), &BackupManager::backupsPruned, this,
            [this](int count) {
        appendLog(QString("🧹 [Yedekleme Temizliği] %1 adet eski yedek (>30 gün) budandı.").arg(count));
    });
    ReferenceMode activeMode = DbManager::instance().getReferenceMode();
    m_tcpServer.setReferenceMode(activeMode);
    updateRefModeBadgeUi(activeMode);
    if (m_goldenRefWidget) {
        m_goldenRefWidget->setActiveReferenceMode(activeMode);
        m_goldenRefWidget->reloadFrequencies();
    }
    onRefreshHistoryClicked();

    // Immediate startup sequence: ICMP Ping -> Web CGI Login -> DeviceInfo & Status Pull
    QTimer::singleShot(250, this, &HostWindow::onRefEthPingSyncClicked);
}

HostWindow::~HostWindow() {
    shutdownHostServices();
}

void HostWindow::shutdownHostServices() {
    if (m_servicesStopped) return;
    m_servicesStopped = true;

    AppConfig::instance().save();
    m_refEthPingTimer.stop();
    m_refTelemetryPollTimer.stop();
    m_beaconBroadcaster.stop();
    m_tcpServer.stop();
    m_refSerial.closePort();
    DbManager::instance().removeClusterNode("HOST_SERVER", "127.0.0.1", AppConfig::instance().tcpPort());
    DbManager::instance().stopPortableMariaDbIfRunning();
}

void HostWindow::closeEvent(QCloseEvent *event) {
    shutdownHostServices();
    event->accept();
    if (!m_returningToLobby) {
        QCoreApplication::quit();
    }
}

void HostWindow::setupMenuBar() {
    QMenu *menuOturum = menuBar()->addMenu("Oturum");
    QAction *actLobby = menuOturum->addAction("🏠 Sunucu Lobisine Dön (HOST / JOIN)");
    connect(actLobby, &QAction::triggered, this, &HostWindow::onReturnLobbyClicked);

    QAction *actLogout = menuOturum->addAction("🔄 Kullanıcı Değiştir");
    actLogout->setShortcut(QKeySequence("Ctrl+L"));
    connect(actLogout, &QAction::triggered, this, &HostWindow::onLogoutClicked);

    menuOturum->addSeparator();
    QAction *actQuit = menuOturum->addAction("Programı Kapat");
    actQuit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    QMenu *menuBaglanti = menuBar()->addMenu("Bağlantı");
    QAction *actEthSync = menuBaglanti->addAction("Referans Ethernet Ping & Veri Yenile");
    actEthSync->setShortcut(QKeySequence("Ctrl+P"));
    connect(actEthSync, &QAction::triggered, this, &HostWindow::onRefEthPingSyncClicked);

    QMenu *menuAraclar = menuBar()->addMenu("Araçlar");
    QAction *actDebug = menuAraclar->addAction("🌐 DRX REST API Debug Konsolu");
    actDebug->setShortcut(QKeySequence("F12"));
    connect(actDebug, &QAction::triggered, this, [this]() {
        auto *debugWin = new DebugRunnerWindow(this);
        debugWin->setAttribute(Qt::WA_DeleteOnClose);
        debugWin->show();
    });

    QAction *actDbTool = menuAraclar->addAction("🗄️ Veritabanı & Kilit Test Aracı");
    actDbTool->setShortcut(QKeySequence("Ctrl+F12"));
    connect(actDbTool, &QAction::triggered, this, [this]() {
        auto *dbWin = new DbDebugWindow(this);
        dbWin->setAttribute(Qt::WA_DeleteOnClose);
        dbWin->show();
    });

    QAction *actWebDb = menuAraclar->addAction("🌍 Web Veritabanı Konsolu");
    actWebDb->setShortcut(QKeySequence("Ctrl+W"));
    connect(actWebDb, &QAction::triggered, this, [this]() {
        // Check if test port 8888 is already active, otherwise use default production port 8000
        int targetPort = 8000;
        QTcpSocket probeTest;
        probeTest.connectToHost("127.0.0.1", 8888);
        if (probeTest.waitForConnected(200)) {
            targetPort = 8888;
        }
        probeTest.abort();

        QTcpSocket probe;
        probe.connectToHost("127.0.0.1", targetPort);
        bool running = probe.waitForConnected(250);
        probe.abort();

        if (!running) {
            QString appDir = QCoreApplication::applicationDirPath();
            QString scriptPath;
            const QStringList candidates = {
                appDir + "/tools/mariadb_portable/web/neta_db_web.py",
                QDir::currentPath() + "/tools/mariadb_portable/web/neta_db_web.py",
                appDir + "/../../tools/mariadb_portable/web/neta_db_web.py"
            };
            for (const QString &c : candidates) {
                if (QFile::exists(c)) {
                    scriptPath = QDir::cleanPath(c);
                    break;
                }
            }
            if (!scriptPath.isEmpty()) {
                QProcess::startDetached("python", QStringList() << scriptPath << "--port" << QString::number(targetPort));
                appendLog(QString("Web Veritabanı Konsolu arka planda başlatılıyor (http://localhost:%1)...").arg(targetPort));
            } else {
                QMessageBox::warning(this, "Web Konsol Bulunamadı",
                                     "neta_db_web.py dosyası bulunamadı. Lütfen tools/mariadb_portable/start_web_db.bat ile başlatın.");
                return;
            }
        }
        QDesktopServices::openUrl(QUrl(QString("http://localhost:%1").arg(targetPort)));
    });

    QMenu *menuAyarlar = menuBar()->addMenu("Ayarlar");
    QAction *actAbout = menuAyarlar->addAction("Hakkında");
    actAbout->setShortcut(QKeySequence("Alt+A"));
    connect(actAbout, &QAction::triggered, this, &HostWindow::onAboutClicked);

    QMenu *menuYardim = menuBar()->addMenu("Yardım");
    QAction *actShortcuts = menuYardim->addAction("⌨️ Kısayollar");
    actShortcuts->setShortcut(QKeySequence("F1"));
    connect(actShortcuts, &QAction::triggered, this, [this]() {
        QMessageBox::information(
            this,
            "Yönetici (Host) — Kısayollar",
            "<h3>⌨️ Klavye & Menü Kısayolları</h3>"
            "<ul>"
            "<li><b>Ctrl + L</b> : Kullanıcı Değiştir</li>"
            "<li><b>Ctrl + P</b> : Referans Ethernet Ping &amp; Veri Yenile</li>"
            "<li><b>Ctrl + W</b> : Web Veritabanı Konsolu</li>"
            "<li><b>Ctrl + Q</b> : Programı Kapat</li>"
            "<li><b>F12</b> : DRX REST API Debug Konsolu</li>"
            "<li><b>Ctrl + F12</b> : Veritabanı &amp; Kilit Test Aracı</li>"
            "<li><b>Alt + A</b> : Hakkında</li>"
            "<li><b>F1</b> : Bu Kısayollar Penceresi</li>"
            "</ul>"
            "<h3>📋 Tablo &amp; Liste Kısayolları</h3>"
            "<ul>"
            "<li><b>Shift + Tık</b> : Aralık (çoklu) satır seçimi</li>"
            "<li><b>Ctrl + Tık</b> : Tek tek çoklu satır seçimi</li>"
            "<li><b>Sağ Tık</b> : Seçili kayıtlar, kullanıcılar veya Golden frekanslar için toplu silme / işlem menüsü</li>"
            "<li><b>Delete</b> : Seçili öğe(leri) silme</li>"
            "<li><b>Çift Tık</b> : Kullanıcıyı pop-up pencere ile düzenleme</li>"
            "</ul>"
            "<h3>📊 SPC Grafiği</h3>"
            "<ul>"
            "<li><b>Fare Üzerine Getir (Hover)</b> : Anlık nokta değerini imleç ucunda görüntüleme</li>"
            "</ul>");
    });

    // Top-right corner: Live Animated Reference DRX Clock Badge
    m_topDrxClockBadge = new DrxClockBadge(this);
    menuBar()->setCornerWidget(m_topDrxClockBadge, Qt::TopRightCorner);
}

void HostWindow::setupUi() {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 6, 8, 6);

    // 1. Top Executive Summary Card (CAT6 Ethernet + Ping status, NO serial ports on main header)
    QGroupBox *grpHeader = new QGroupBox("🏢 Fabrika Yönetim Sunucusu & Referans Çanak (Ethernet)", central);
    QHBoxLayout *topLayout = new QHBoxLayout(grpHeader);
    topLayout->setSpacing(8);

    QLabel *lblLogo = new QLabel(grpHeader);
    lblLogo->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 3px 10px;");
    QPixmap pix(":/resources/images/logo.png");
    if (!pix.isNull()) {
        lblLogo->setPixmap(pix.scaled(118, 34, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        lblLogo->setText("<b style='color:#064E3B; font-size:13pt;'>NETA</b>");
    }

    m_lblServerStatus = new QLabel(QString("🟢 Host Aktif (Port %1)").arg(AppConfig::instance().tcpPort()), grpHeader);
    m_lblServerStatus->setStyleSheet(StyleHelper::getPastelGreenStyle());

    m_lblRefModeBadge = new QLabel("🟢 Mod: Canlı Referans", grpHeader);
    m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());

    m_lblRefPingBadge = new QLabel("📶 Ref Ping: Test...", grpHeader);
    m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());

    m_lblRefTelemetry = new QLabel("🔴 Ref Web: Bekleniyor", grpHeader);
    m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelRedStyle());

    QLabel *lblRoleBadge = new QLabel("HOST YÖNETİCİ", grpHeader);
    lblRoleBadge->setStyleSheet("background-color: #059669; color: white; border-radius: 4px; padding: 3px 8px; font-weight: bold;");

    m_lblOperatorName = new QLabel(AppConfig::instance().currentUserFullName(), grpHeader);
    m_lblOperatorName->setStyleSheet("color: #34D399; font-weight: bold;");

    QLabel *lblRefIpTitle = new QLabel("🌐 Ref DRX IP:", grpHeader);
    lblRefIpTitle->setStyleSheet("color: #34D399; font-weight: bold;");

    m_txtHeaderRefIp = new QLineEdit(AppConfig::instance().refDrxEndpoint(), grpHeader);
    m_txtHeaderRefIp->setPlaceholderText("192.168.0.167 veya 127.0.0.1:8080");
    m_txtHeaderRefIp->setFixedWidth(185);
    m_txtHeaderRefIp->setToolTip("Referans DRX9000 IP adresi (Simülatör için örn: 127.0.0.1:8080)");
    m_txtHeaderRefIp->setStyleSheet("background-color: #0E2219; color: #34D399; border: 1px solid #1A3D2B; border-radius: 4px; padding: 4px 8px; font-weight: bold;");
    auto applyHeaderRefIp = [this]() {
        if (!m_txtHeaderRefIp) return;
        const QString enteredIp = m_txtHeaderRefIp->text().trimmed();
        if (enteredIp.isEmpty()) return;
        AppConfig &cfg = AppConfig::instance();
        if (enteredIp != cfg.refDrxEndpoint()) {
            cfg.setRefDrxHost(enteredIp);
            cfg.save();
            m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
            if (m_txtRefDrxIp) {
                m_txtRefDrxIp->blockSignals(true);
                m_txtRefDrxIp->setText(cfg.refDrxHost());
                m_txtRefDrxIp->blockSignals(false);
            }
            if (m_spinRefDrxPort) {
                m_spinRefDrxPort->blockSignals(true);
                m_spinRefDrxPort->setValue(cfg.refDrxPort());
                m_spinRefDrxPort->blockSignals(false);
            }
            setStatusMessage(QString("Referans DRX IP güncellendi: %1").arg(cfg.refDrxEndpoint()));
        }
    };
    connect(m_txtHeaderRefIp, &QLineEdit::editingFinished, this, applyHeaderRefIp);
    connect(m_txtHeaderRefIp, &QLineEdit::returnPressed, this, &HostWindow::onRefEthPingSyncClicked);

    m_btnRefEthPingSync = new QPushButton("🔌 Ref Ping & Bağlan (ETH)", grpHeader);
    m_btnRefEthPingSync->setCursor(Qt::PointingHandCursor);
    m_btnRefEthPingSync->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnRefEthPingSync, &QPushButton::clicked, this, &HostWindow::onRefEthPingSyncClicked);

    auto *btnLaunchSim = new QPushButton("🧪 Simülatörü Aç", grpHeader);
    btnLaunchSim->setCursor(Qt::PointingHandCursor);
    btnLaunchSim->setToolTip("NETA DRX9000 TCP Simülatörünü başlat");
    btnLaunchSim->setStyleSheet("background-color: #1D4ED8; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(btnLaunchSim, &QPushButton::clicked, this, [this]() { AppConfig::launchSimulator(this); });

    topLayout->addWidget(lblLogo);
    topLayout->addWidget(m_lblServerStatus);
    topLayout->addWidget(m_lblRefModeBadge);
    topLayout->addWidget(m_lblRefPingBadge);
    topLayout->addWidget(m_lblRefTelemetry);
    topLayout->addWidget(lblRoleBadge);
    topLayout->addWidget(m_lblOperatorName);
    topLayout->addStretch();
    topLayout->addWidget(lblRefIpTitle);
    topLayout->addWidget(m_txtHeaderRefIp);
    topLayout->addWidget(m_btnRefEthPingSync);
    topLayout->addWidget(btnLaunchSim);
    mainLayout->addWidget(grpHeader);

    // 2. Sidebar + Stacked Workspace
    QSplitter *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setHandleWidth(2);
    splitter->setChildrenCollapsible(false);

    m_sideNav = new QListWidget(splitter);
    m_sideNav->setObjectName("sideNav");
    m_sideNav->setFixedWidth(215);
    m_sideNav->setSpacing(2);
    m_sideNav->setFrameShape(QFrame::NoFrame);

    QListWidgetItem *headerItem = new QListWidgetItem("  🏢  HOST YÖNETİCİ");
    headerItem->setFlags(Qt::NoItemFlags);
    headerItem->setFont(QFont("Segoe UI", 10, QFont::Bold));
    headerItem->setForeground(QColor("#34D399"));
    headerItem->setBackground(QColor("#064E3B"));
    headerItem->setSizeHint(QSize(215, 42));
    m_sideNav->addItem(headerItem);

    QListWidgetItem *sepItem = new QListWidgetItem();
    sepItem->setFlags(Qt::NoItemFlags);
    sepItem->setSizeHint(QSize(215, 1));
    sepItem->setBackground(QColor("#1A3D2B"));
    m_sideNav->addItem(sepItem);

    auto addNavItem = [&](const QString &text) {
        QListWidgetItem *item = new QListWidgetItem(text);
        item->setSizeHint(QSize(215, 46));
        m_sideNav->addItem(item);
    };
    addNavItem("  🎮  Canlı Test Lobisi");
    addNavItem("  🌟  Referans & Golden Ölçüm");
    addNavItem("  📊  Test Geçmişi & SPC");
    addNavItem("  📦  İş Emirleri");
    addNavItem("  🗄️  Veritabanı (SQL)");
    addNavItem("  🔐  Kullanıcı & Yetki");
    addNavItem("  📜  Sistem Logları");
    addNavItem("  ⚙️  Sistem Ayarları");

    m_sideNav->setCurrentRow(3);

    m_pageStack = new QStackedWidget(splitter);
    setupLobbyPage();
    setupGoldenPage();
    setupHistoryPage();
    setupOrdersPage();
    setupDatabasePage();
    m_userManager = new UserManagerWidget(this);
    connect(m_userManager, &UserManagerWidget::logMessage, this, &HostWindow::appendLog);
    connect(m_userManager, &UserManagerWidget::usersUpdated, &m_tcpServer, &TcpServer::broadcastUserRoleUpdates);
    setupSettingsPage();
    setupLogsPage();

    m_pageStack->addWidget(m_pageLobby);      // index 0
    m_pageStack->addWidget(m_pageGolden);     // index 1
    m_pageStack->addWidget(m_pageHistory);    // index 2
    m_pageStack->addWidget(m_pageOrders);     // index 3
    m_pageStack->addWidget(m_pageDatabase);   // index 4
    m_pageStack->addWidget(m_userManager);    // index 5
    m_pageStack->addWidget(m_pageLogs);       // index 6
    m_pageStack->addWidget(m_pageSettings);   // index 7

    splitter->addWidget(m_sideNav);
    splitter->addWidget(m_pageStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter, 1);

    connect(m_sideNav, &QListWidget::currentRowChanged, this, &HostWindow::onNavItemChanged);

    // 3. Clean Single-Line Status Bar
    m_statusLabel = new QLabel("Hazır", this);
    m_statusLabel->setStyleSheet("color: #A7C4B5; padding-left: 4px;");
    statusBar()->addWidget(m_statusLabel, 1);

    QLabel *lblFooterRight = new QLabel(
        QString("NETA Elektronik A.Ş.  |  Geliştirici: %1  |  v%2")
            .arg(NETA_APP_DEVELOPER, NETA_APP_VERSION), this);
    lblFooterRight->setStyleSheet("color: #4B7A66; padding-right: 6px; font-size: 8.5pt;");
    statusBar()->addPermanentWidget(lblFooterRight);

    loadWorkOrders();
}

/**
 * @brief Switches the stacked page view when a sidebar navigation row is selected.
 * @param row Selected row index inside m_sideNav (rows 0..1 are header & separator).
 */
void HostWindow::onNavItemChanged(int row) {
    if (!m_pageStack) return;
    int pageIdx = row - 2;
    if (pageIdx >= 0 && pageIdx < m_pageStack->count()) {
        m_pageStack->setCurrentIndex(pageIdx);
        if (m_pageStack->widget(pageIdx) == m_userManager && m_userManager) {
            m_userManager->reloadUsers();
        } else if (m_pageStack->widget(pageIdx) == m_pageHistory && m_historyWidget) {
            m_historyWidget->refreshHistory();
        } else if (m_pageStack->widget(pageIdx) == m_pageDatabase && m_dbViewerWidget) {
            m_dbViewerWidget->refreshDbTables();
        } else if (m_pageStack->widget(pageIdx) == m_pageLogs && m_auditLogWidget) {
            m_auditLogWidget->refreshAuditLogs();
        } else if (m_pageStack->widget(pageIdx) == m_pageOrders) {
            loadWorkOrders();
        }
    }
}

void HostWindow::setupLobbyPage() {
    m_pageLobby = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageLobby);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(10);

    QGroupBox *grpArbiter = new QGroupBox("🔒 Merkezi Referans Kilit Durumu (Mutex Arbiter)", m_pageLobby);
    QHBoxLayout *arbLayout = new QHBoxLayout(grpArbiter);
    m_lblArbiterStatus = new QLabel("🟢 Referans Alıcısı BOŞTA (Tüm İstasyonlar İçin Müsait)", grpArbiter);
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");
    arbLayout->addWidget(m_lblArbiterStatus);
    vbox->addWidget(grpArbiter);

    // Live Station Cards (Unified Lobby View)
    QGroupBox *grpStations = new QGroupBox("🖥️ Bağlı Test İstasyonları (Canlı Kartlar)", m_pageLobby);
    QVBoxLayout *stBox = new QVBoxLayout(grpStations);

    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setSpacing(10);
    m_lblPcSummaryStats = new QLabel("Toplam PC: 0  |  🟢 Çevrimiçi: 0  |  🧪 Aktif Testte: 0", grpStations);
    m_lblPcSummaryStats->setStyleSheet(StyleHelper::getPastelGreenStyle());
    topRow->addWidget(m_lblPcSummaryStats);
    topRow->addStretch();
    stBox->addLayout(topRow);

    m_lblNoStations = new QLabel(
        "⏳ Henüz bağlı test istasyonu yok. Ağdaki istasyonlar Lobiden JOIN yaptığında kartları burada otomatik oluşacaktır.",
        grpStations);
    m_lblNoStations->setAlignment(Qt::AlignCenter);
    m_lblNoStations->setStyleSheet("color: #A7C4B5; padding: 24px; font-size: 10.5pt; font-style: italic;");
    stBox->addWidget(m_lblNoStations);

    QScrollArea *scroll = new QScrollArea(grpStations);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_stationsContainer = new QWidget(scroll);
    m_stationsGrid = new QGridLayout(m_stationsContainer);
    m_stationsGrid->setSpacing(12);
    m_stationsGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(m_stationsContainer);
    stBox->addWidget(scroll, 1);

    vbox->addWidget(grpStations, 1);

    updateLobbySummary();
}

void HostWindow::ensureStationCard(int stationId, const QString &stationName) {
    if (stationId <= 0 || !m_stationsGrid) return;
    if (m_lblNoStations) m_lblNoStations->setVisible(false);

    if (m_stationWidgets.contains(stationId)) {
        if (!stationName.isEmpty() && m_stationWidgets[stationId].lblTitle) {
            m_stationWidgets[stationId].lblTitle->setText(
                QString("🖥️ İSTASYON %1 (%2)").arg(stationId).arg(stationName));
        }
        return;
    }

    QFrame *card = new QFrame(m_stationsContainer);
    card->setMinimumWidth(260);
    card->setStyleSheet("background-color: #112D21; border: 1px solid #1A3D2B; border-radius: 6px; padding: 12px;");
    QVBoxLayout *cardVBox = new QVBoxLayout(card);
    cardVBox->setSpacing(8);

    QString titleStr = stationName.isEmpty()
                           ? QString("🖥️ İSTASYON %1").arg(stationId)
                           : QString("🖥️ İSTASYON %1 (%2)").arg(stationId).arg(stationName);
    QLabel *lblTitle = new QLabel(titleStr, card);
    lblTitle->setStyleSheet("font-size: 12pt; font-weight: bold; color: #34D399;");

    StationCardWidgets widgets;
    widgets.cardFrame = card;
    widgets.lblTitle = lblTitle;
    widgets.lblConn = new QLabel("Bağlantı: 🟢 ÇEVRİMİÇİ", card);
    widgets.lblConn->setStyleSheet("font-weight: bold; color: #34D399;");

    widgets.lblState = new QLabel("DURUM: BAĞLANDI (BOŞTA)", card);
    widgets.lblState->setStyleSheet(StyleHelper::getPastelBlueStyle());

    widgets.lblIp = new QLabel("IP Adresi: --", card);
    widgets.lblIp->setStyleSheet("color: #A7C4B5;");

    widgets.lblSn = new QLabel("Anten Barkodu: --", card);
    widgets.lblSn->setStyleSheet("font-weight: bold; color: #E6F4EE;");

    widgets.lblChannel = new QLabel("Aktif Kanal: --", card);
    widgets.lblChannel->setStyleSheet("color: #E6F4EE;");

    widgets.lblHeartbeat = new QLabel("Son Sinyal: --", card);
    widgets.lblHeartbeat->setStyleSheet("color: #6B7280; font-size: 8.5pt;");

    cardVBox->addWidget(widgets.lblTitle);
    cardVBox->addWidget(widgets.lblConn);
    cardVBox->addWidget(widgets.lblState);
    cardVBox->addWidget(widgets.lblIp);
    cardVBox->addWidget(widgets.lblSn);
    cardVBox->addWidget(widgets.lblChannel);
    cardVBox->addWidget(widgets.lblHeartbeat);
    cardVBox->addStretch();

    int idx = m_stationWidgets.size();
    int row = idx / 3;
    int col = idx % 3;
    m_stationsGrid->addWidget(card, row, col);
    m_stationWidgets[stationId] = widgets;
}

void HostWindow::setupGoldenPage() {
    m_goldenRefWidget = new GoldenReferenceWidget(m_refStream, this);
    connect(m_goldenRefWidget, &GoldenReferenceWidget::referenceModeApplyRequested,
            this, &HostWindow::onReferenceModeApplyRequested);
    connect(m_goldenRefWidget, &GoldenReferenceWidget::logMessage,
            this, &HostWindow::appendLog);
    m_pageGolden = m_goldenRefWidget;
}

void HostWindow::setupDatabasePage() {
    m_pageDatabase = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageDatabase);
    vbox->setContentsMargins(0, 0, 0, 0);
    m_dbViewerWidget = new TestHistoryWidget(TestHistoryWidget::ViewMode::DatabaseOnly, m_pageDatabase);
    connect(m_dbViewerWidget, &TestHistoryWidget::logMessage, this, &HostWindow::appendLog);
    vbox->addWidget(m_dbViewerWidget);
}

void HostWindow::updateLobbySummary() {
    const auto liveMap = m_tcpServer.connectedStations();
    for (auto it = liveMap.constBegin(); it != liveMap.constEnd(); ++it) {
        m_knownStations[it.key()] = it.value();
    }

    int totalCount = m_knownStations.size();
    int onlineCount = 0;
    int testingCount = 0;

    for (auto it = m_knownStations.constBegin(); it != m_knownStations.constEnd(); ++it) {
        const auto &st = it.value();
        if (st.isConnected) onlineCount++;
        if (st.isConnected && st.currentState.contains("TEST", Qt::CaseInsensitive)) testingCount++;
    }

    if (m_lblPcSummaryStats) {
        m_lblPcSummaryStats->setText(
            QString("Toplam PC: %1  |  🟢 Çevrimiçi: %2  |  🧪 Aktif Testte: %3")
                .arg(totalCount).arg(onlineCount).arg(testingCount));
    }
}

void HostWindow::setupHistoryPage() {
    m_pageHistory = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageHistory);
    vbox->setContentsMargins(0, 0, 0, 0);
    m_historyWidget = new TestHistoryWidget(m_pageHistory);
    connect(m_historyWidget, &TestHistoryWidget::logMessage, this, &HostWindow::appendLog);
    vbox->addWidget(m_historyWidget);
}

void HostWindow::setupOrdersPage() {
    m_pageOrders = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageOrders);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(8);

    // Toolbar
    QHBoxLayout *toolBar = new QHBoxLayout();
    toolBar->setSpacing(8);

    QPushButton *btnAddOrder = new QPushButton("➕ Yeni İş Emri Ekle", m_pageOrders);
    btnAddOrder->setStyleSheet("background-color: #059669; color: white; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(btnAddOrder, &QPushButton::clicked, this, &HostWindow::onAddWorkOrderClicked);
    toolBar->addWidget(btnAddOrder);

    QPushButton *btnCompleteOrder = new QPushButton("✔️ Tamamlandı Olarak İşaretle", m_pageOrders);
    btnCompleteOrder->setStyleSheet("background-color: #0284C7; color: white; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(btnCompleteOrder, &QPushButton::clicked, this, &HostWindow::onCompleteWorkOrderClicked);
    toolBar->addWidget(btnCompleteOrder);

    QPushButton *btnDeleteOrder = new QPushButton("🗑️ Seçili İş Emrini Sil", m_pageOrders);
    btnDeleteOrder->setStyleSheet("background-color: #B91C1C; color: white; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(btnDeleteOrder, &QPushButton::clicked, this, &HostWindow::onDeleteWorkOrderClicked);
    toolBar->addWidget(btnDeleteOrder);

    QPushButton *btnRefresh = new QPushButton("🔄 Listeyi Yenile", m_pageOrders);
    connect(btnRefresh, &QPushButton::clicked, this, &HostWindow::loadWorkOrders);
    toolBar->addWidget(btnRefresh);

    toolBar->addStretch();
    vbox->addLayout(toolBar);

    m_tableOrders = new QTableWidget(m_pageOrders);
    m_tableOrders->setColumnCount(11);
    m_tableOrders->setHorizontalHeaderLabels({
        "ID", "Sipariş No", "Müşteri Adı", "Ürün Kodu", "Uydu Profili",
        "Hedef Adet", "Tamamlanan", "Başarılı", "Hatalı", "Durum", "Notlar"
    });
    m_tableOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableOrders->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tableOrders->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tableOrders->verticalHeader()->setVisible(false);
    m_tableOrders->setAlternatingRowColors(true);
    m_tableOrders->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableOrders->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableOrders->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vbox->addWidget(m_tableOrders);
}

void HostWindow::loadWorkOrders() {
    if (!m_tableOrders) return;
    QVector<WorkOrderInfo> orders = DbManager::instance().getAllWorkOrders();
    QVector<SatelliteProfileInfo> profiles = DbManager::instance().getProfiles();
    QMap<int, QString> profMap;
    for (const auto &p : profiles) profMap[p.id] = p.profileName;

    m_tableOrders->setRowCount(0);
    for (int i = 0; i < orders.size(); ++i) {
        const auto &o = orders[i];
        m_tableOrders->insertRow(i);
        m_tableOrders->setItem(i, 0, new QTableWidgetItem(QString::number(o.id)));
        m_tableOrders->setItem(i, 1, new QTableWidgetItem(o.orderNo));
        m_tableOrders->setItem(i, 2, new QTableWidgetItem(o.customerName));
        m_tableOrders->setItem(i, 3, new QTableWidgetItem(o.productCode));
        m_tableOrders->setItem(i, 4, new QTableWidgetItem(profMap.value(o.satelliteProfileId, QString("Profil #%1").arg(o.satelliteProfileId))));
        m_tableOrders->setItem(i, 5, new QTableWidgetItem(QString::number(o.targetQuantity)));
        m_tableOrders->setItem(i, 6, new QTableWidgetItem(QString::number(o.completedQuantity)));
        m_tableOrders->setItem(i, 7, new QTableWidgetItem(QString::number(o.passedQuantity)));
        m_tableOrders->setItem(i, 8, new QTableWidgetItem(QString::number(o.failedQuantity)));

        QTableWidgetItem *itemStatus = new QTableWidgetItem(o.status);
        if (o.status == "COMPLETED") {
            itemStatus->setForeground(QBrush(QColor("#34D399")));
        } else if (o.status == "IN_PROGRESS") {
            itemStatus->setForeground(QBrush(QColor("#38BDF8")));
        } else {
            itemStatus->setForeground(QBrush(QColor("#FBBF24")));
        }
        itemStatus->setTextAlignment(Qt::AlignCenter);
        m_tableOrders->setItem(i, 9, itemStatus);
        m_tableOrders->setItem(i, 10, new QTableWidgetItem(o.notes));
    }
}

void HostWindow::onAddWorkOrderClicked() {
    QDialog dlg(this);
    dlg.setWindowTitle("Yeni İş Emri Oluştur");
    dlg.setMinimumWidth(440);
    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(10);

    QFormLayout *form = new QFormLayout();
    form->setSpacing(8);

    QLineEdit *txtOrderNo = new QLineEdit(&dlg);
    txtOrderNo->setText(QString("NETA-%1-%2").arg(QDate::currentDate().year()).arg(QTime::currentTime().msec(), 3, 10, QChar('0')));
    form->addRow("Sipariş / İş Emri No *:", txtOrderNo);

    QLineEdit *txtCustomer = new QLineEdit("Genel Üretim", &dlg);
    form->addRow("Müşteri Adı:", txtCustomer);

    QLineEdit *txtProduct = new QLineEdit("DRX9000-ANT-V1", &dlg);
    form->addRow("Ürün Kodu:", txtProduct);

    QComboBox *comboProfiles = new QComboBox(&dlg);
    auto profiles = DbManager::instance().getProfiles();
    for (const auto &p : profiles) {
        comboProfiles->addItem(QString("%1 (%2)").arg(p.profileName, p.orbitalPosition), p.id);
    }
    form->addRow("Uydu Profili *:", comboProfiles);

    QSpinBox *spinTarget = new QSpinBox(&dlg);
    spinTarget->setRange(1, 100000);
    spinTarget->setValue(100);
    form->addRow("Hedef Üretim Adedi *:", spinTarget);

    QLineEdit *txtNotes = new QLineEdit(&dlg);
    txtNotes->setPlaceholderText("Opsiyonel parti veya sipariş notu...");
    form->addRow("Notlar:", txtNotes);

    layout->addLayout(form);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton("💾 Kaydet ve İstasyonlara Sun", &dlg);
    btnSave->setStyleSheet("background-color: #059669; color: white; font-weight: bold; padding: 6px 14px; border-radius: 4px;");
    QPushButton *btnCancel = new QPushButton("İptal", &dlg);
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnSave);
    layout->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnSave, &QPushButton::clicked, [&]() {
        if (txtOrderNo->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, "Eksik Alan", "Sipariş / İş Emri No boş bırakılamaz!");
            return;
        }
        WorkOrderInfo wo;
        wo.orderNo = txtOrderNo->text().trimmed();
        wo.customerName = txtCustomer->text().trimmed();
        wo.productCode = txtProduct->text().trimmed();
        wo.satelliteProfileId = comboProfiles->currentData().toInt();
        wo.targetQuantity = spinTarget->value();
        wo.status = "IN_PROGRESS";
        wo.notes = txtNotes->text().trimmed();

        if (DbManager::instance().saveWorkOrder(wo)) {
            dlg.accept();
        } else {
            QMessageBox::warning(&dlg, "Hata", "İş emri kaydedilemedi: " + DbManager::instance().lastError());
        }
    });

    if (dlg.exec() == QDialog::Accepted) {
        loadWorkOrders();
        appendLog("[İş Emri] Yeni iş emri oluşturuldu: " + txtOrderNo->text().trimmed());
    }
}

void HostWindow::onCompleteWorkOrderClicked() {
    int row = m_tableOrders ? m_tableOrders->currentRow() : -1;
    if (row < 0) {
        QMessageBox::information(this, "Seçim Yapılmadı", "Lütfen tamamlandı olarak işaretlemek istediğiniz iş emrini tablodan seçiniz.");
        return;
    }
    int woId = m_tableOrders->item(row, 0)->text().toInt();
    QString orderNo = m_tableOrders->item(row, 1)->text();

    if (DbManager::instance().setWorkOrderStatus(woId, "COMPLETED")) {
        loadWorkOrders();
        appendLog(QString("[İş Emri] İş emri tamamlandı olarak güncellendi: %1 (ID: #%2)").arg(orderNo).arg(woId));
        QMessageBox::information(this, "Tamamlandı", QString("İş Emri '%1' tamamlandı olarak işaretlendi.").arg(orderNo));
    }
}

void HostWindow::onDeleteWorkOrderClicked() {
    int row = m_tableOrders ? m_tableOrders->currentRow() : -1;
    if (row < 0) {
        QMessageBox::information(this, "Seçim Yapılmadı", "Lütfen silmek istediğiniz iş emrini tablodan seçiniz.");
        return;
    }
    int woId = m_tableOrders->item(row, 0)->text().toInt();
    QString orderNo = m_tableOrders->item(row, 1)->text();

    auto ans = QMessageBox::question(this, "İş Emrini Sil",
                                     QString("'%1' numaralı iş emrini silmek istediğinize emin misiniz?").arg(orderNo),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans == QMessageBox::Yes) {
        if (DbManager::instance().deleteWorkOrder(woId)) {
            loadWorkOrders();
            appendLog(QString("[İş Emri] İş emri silindi: %1 (ID: #%2)").arg(orderNo).arg(woId));
        } else {
            QMessageBox::warning(this, "Hata", "İş emri silinemedi: " + DbManager::instance().lastError());
        }
    }
}

void HostWindow::setupSettingsPage() {
    m_pageSettings = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageSettings);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(10);

    QGridLayout *cardsGrid = new QGridLayout();
    cardsGrid->setSpacing(10);

    // Card 1: Test Parameters
    QGroupBox *grpTestSettings = new QGroupBox("⚖️ Test ve Tolerans Parametreleri", m_pageSettings);
    QFormLayout *testForm = new QFormLayout(grpTestSettings);
    testForm->setSpacing(10);

    m_spinLockTimeout = new QSpinBox(grpTestSettings);
    m_spinLockTimeout->setRange(10, 600);
    m_spinLockTimeout->setSuffix(" sn");
    m_spinLockTimeout->setValue(AppConfig::instance().lockTimeoutSec());

    m_spinSampleDuration = new QSpinBox(grpTestSettings);
    m_spinSampleDuration->setRange(1, 30);
    m_spinSampleDuration->setSuffix(" sn");
    m_spinSampleDuration->setValue(AppConfig::instance().sampleDurationSec());

    m_spinTolerance = new QDoubleSpinBox(grpTestSettings);
    m_spinTolerance->setRange(0.1, 10.0);
    m_spinTolerance->setSingleStep(0.1);
    m_spinTolerance->setSuffix(" dB");
    m_spinTolerance->setValue(AppConfig::instance().defaultToleranceDb());

    m_spinHostTcpPort = new QSpinBox(grpTestSettings);
    m_spinHostTcpPort->setRange(1024, 65535);
    m_spinHostTcpPort->setValue(AppConfig::instance().tcpPort());

    m_spinHostUdpPort = new QSpinBox(grpTestSettings);
    m_spinHostUdpPort->setRange(1024, 65535);
    m_spinHostUdpPort->setValue(AppConfig::instance().udpBeaconPort());

    QPushButton *btnSave = new QPushButton("💾 Ayarları Kaydet", grpTestSettings);
    btnSave->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 16px; font-weight: bold;");
    connect(btnSave, &QPushButton::clicked, this, &HostWindow::onSaveSettingsClicked);

    testForm->addRow("Maksimum Kilit Zaman Aşımı:", m_spinLockTimeout);
    testForm->addRow("Kanal Başına Örnekleme Süresi:", m_spinSampleDuration);
    testForm->addRow("Varsayılan Tolerans Payı:", m_spinTolerance);
    testForm->addRow("TCP Sunucu Portu:", m_spinHostTcpPort);
    testForm->addRow("UDP Lobi Yayın Portu:", m_spinHostUdpPort);
    testForm->addRow(btnSave);
    cardsGrid->addWidget(grpTestSettings, 0, 0);

    // Card 2: Reference DRX Ethernet (Primary)
    QGroupBox *grpRefEth = new QGroupBox("🌐 Referans DRX9000 Ethernet & Web CGI Ayarları", m_pageSettings);
    QFormLayout *refForm = new QFormLayout(grpRefEth);
    refForm->setSpacing(10);

    m_txtRefDrxIp = new QLineEdit(AppConfig::instance().refDrxHost(), grpRefEth);
    m_spinRefDrxPort = new QSpinBox(grpRefEth);
    m_spinRefDrxPort->setRange(1, 65535);
    m_spinRefDrxPort->setValue(AppConfig::instance().refDrxPort());
    m_txtRefDrxUser = new QLineEdit(AppConfig::instance().drxUsername(), grpRefEth);
    m_txtRefDrxPass = new QLineEdit(AppConfig::instance().drxPassword(), grpRefEth);
    m_txtRefDrxPass->setEchoMode(QLineEdit::Password);

    connect(m_txtRefDrxIp, &QLineEdit::editingFinished, this, [this]() {
        const QString ip = m_txtRefDrxIp->text().trimmed();
        if (ip.isEmpty()) return;
        AppConfig &cfg = AppConfig::instance();
        cfg.setRefDrxHost(ip);
        cfg.save();
        m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
        if (m_txtHeaderRefIp) {
            m_txtHeaderRefIp->blockSignals(true);
            m_txtHeaderRefIp->setText(ip);
            m_txtHeaderRefIp->blockSignals(false);
        }
    });

    QPushButton *btnSaveRefEth = new QPushButton("🔗 Kaydet & Ref Ping Testi Yap", grpRefEth);
    btnSaveRefEth->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnSaveRefEth, &QPushButton::clicked, this, &HostWindow::onSaveRefWebSettingsClicked);

    refForm->addRow("Referans (Golden) DRX IP:", m_txtRefDrxIp);
    refForm->addRow("Referans DRX Port:", m_spinRefDrxPort);
    refForm->addRow("Web Kullanıcı Adı:", m_txtRefDrxUser);
    refForm->addRow("Web Şifresi:", m_txtRefDrxPass);
    refForm->addRow(btnSaveRefEth);
    cardsGrid->addWidget(grpRefEth, 0, 1);

    // Card 3: Database Management
    QGroupBox *grpDbSettings = new QGroupBox("🗄️ Merkezi Veritabanı & MariaDB Servisi", m_pageSettings);
    QVBoxLayout *vboxDb = new QVBoxLayout(grpDbSettings);
    vboxDb->setSpacing(10);

    QHBoxLayout *statusRow = new QHBoxLayout();
    QLabel *lblDbStatusTitle = new QLabel("Servis Durumu:", grpDbSettings);
    m_lblDbStatus = new QLabel("Kontrol ediliyor...", grpDbSettings);
    m_lblDbStatus->setStyleSheet("font-weight: bold; color: #FBBF24;");
    QPushButton *btnCheckDb = new QPushButton("🔄 Kontrol Et", grpDbSettings);
    connect(btnCheckDb, &QPushButton::clicked, this, &HostWindow::onCheckDbClicked);
    statusRow->addWidget(lblDbStatusTitle);
    statusRow->addWidget(m_lblDbStatus);
    statusRow->addStretch();
    statusRow->addWidget(btnCheckDb);
    vboxDb->addLayout(statusRow);

    QFormLayout *dbForm = new QFormLayout();
    m_txtDbHost = new QLineEdit(AppConfig::instance().dbHost(), grpDbSettings);
    m_spinDbPort = new QSpinBox(grpDbSettings);
    m_spinDbPort->setRange(1, 65535);
    m_spinDbPort->setValue(AppConfig::instance().dbPort());
    m_txtDbName = new QLineEdit(AppConfig::instance().dbName(), grpDbSettings);
    dbForm->addRow("Sunucu Host IP:", m_txtDbHost);
    dbForm->addRow("Veritabanı Portu:", m_spinDbPort);
    dbForm->addRow("Veritabanı Adı:", m_txtDbName);
    vboxDb->addLayout(dbForm);

    QHBoxLayout *dbActionRow = new QHBoxLayout();
    QPushButton *btnInstallMysql = new QPushButton("🔌 Veritabanına Bağlan", grpDbSettings);
    btnInstallMysql->setStyleSheet("background-color: #0284C7; color: #FFFFFF; border: none; border-radius: 4px; padding: 8px 14px; font-weight: bold;");
    connect(btnInstallMysql, &QPushButton::clicked, this, &HostWindow::onInstallMysqlClicked);

    QPushButton *btnResetDb = new QPushButton("📦 Şemayı / Veriyi Yenile", grpDbSettings);
    connect(btnResetDb, &QPushButton::clicked, this, &HostWindow::onResetDbClicked);

    dbActionRow->addWidget(btnInstallMysql);
    dbActionRow->addWidget(btnResetDb);
    vboxDb->addLayout(dbActionRow);

    QHBoxLayout *backupActionRow = new QHBoxLayout();
    m_btnBackupNow = new QPushButton("💾 Veritabanını Şimdi Yedekle", grpDbSettings);
    m_btnBackupNow->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 7px 12px; font-weight: bold;");
    connect(m_btnBackupNow, &QPushButton::clicked, this, &HostWindow::onBackupDbNowClicked);

    m_btnOpenBackupDir = new QPushButton("📂 Yedek Klasörü", grpDbSettings);
    m_btnOpenBackupDir->setStyleSheet("background-color: #374151; color: #E5E7EB; border: 1px solid #4B5563; border-radius: 4px; padding: 7px 12px;");
    connect(m_btnOpenBackupDir, &QPushButton::clicked, this, &HostWindow::onOpenBackupDirClicked);

    backupActionRow->addWidget(m_btnBackupNow);
    backupActionRow->addWidget(m_btnOpenBackupDir);
    vboxDb->addLayout(backupActionRow);

    m_lblLastBackupInfo = new QLabel(grpDbSettings);
    m_lblLastBackupInfo->setStyleSheet("color: #9CA3AF; font-size: 8pt;");
    QFileInfoList backups = BackupManager::instance().listBackups();
    if (!backups.isEmpty()) {
        m_lblLastBackupInfo->setText(QString("Son Yedek: %1 (%2 KB) | Otomatik: Aktif (24s)")
            .arg(backups.first().fileName())
            .arg(backups.first().size() / 1024));
    } else {
        m_lblLastBackupInfo->setText("Son Yedek: Henüz yok | Otomatik: Aktif (24s)");
    }
    vboxDb->addWidget(m_lblLastBackupInfo);

    cardsGrid->addWidget(grpDbSettings, 1, 0);

    // Card 4: Optional Legacy RS232 Fallback for Reference DRX
    QGroupBox *grpRefSerial = new QGroupBox("🔧 Gelişmiş: Referans RS232 Yedek Seri Port (Opsiyonel)", m_pageSettings);
    QFormLayout *serForm = new QFormLayout(grpRefSerial);
    serForm->setSpacing(10);

    QLabel *lblSerNote = new QLabel("Birincil iletişim Ethernet (CAT6) Web CGI üzerindendir. Sadece acil debug için RS232 seçin.", grpRefSerial);
    lblSerNote->setWordWrap(true);
    lblSerNote->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");

    QHBoxLayout *serCtrlRow = new QHBoxLayout();
    m_comboSerialPorts = new QComboBox(grpRefSerial);
    QPushButton *btnRefresh = new QPushButton("🔄", grpRefSerial);
    btnRefresh->setFixedSize(32, 28);
    connect(btnRefresh, &QPushButton::clicked, this, &HostWindow::onRefreshPortsClicked);
    m_btnConnectRefSerial = new QPushButton("RS232 Bağlan", grpRefSerial);
    m_btnConnectRefSerial->setStyleSheet("background-color: #059669; color: #FFFFFF; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold;");
    connect(m_btnConnectRefSerial, &QPushButton::clicked, this, &HostWindow::onConnectRefSerialClicked);

    serCtrlRow->addWidget(m_comboSerialPorts, 1);
    serCtrlRow->addWidget(btnRefresh);
    serCtrlRow->addWidget(m_btnConnectRefSerial);

    serForm->addRow(lblSerNote);
    serForm->addRow("COM Port Seçimi:", serCtrlRow);
    cardsGrid->addWidget(grpRefSerial, 1, 1);

    vbox->addLayout(cardsGrid);
    vbox->addStretch();

    // Auto-save settings to QSettings (.ini) on value changes
    auto autoSaveHostSettings = [this]() {
        AppConfig &cfg = AppConfig::instance();
        cfg.setLockTimeoutSec(m_spinLockTimeout->value());
        cfg.setSampleDurationSec(m_spinSampleDuration->value());
        cfg.setDefaultToleranceDb(m_spinTolerance->value());
        cfg.setRefDrxHost(m_txtRefDrxIp->text().trimmed());
        cfg.setDrxUsername(m_txtRefDrxUser->text().trimmed());
        cfg.setDrxPassword(m_txtRefDrxPass->text());
        cfg.setDbHost(m_txtDbHost->text().trimmed());
        cfg.setDbName(m_txtDbName->text().trimmed());
        cfg.save();
    };
    connect(m_spinLockTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, autoSaveHostSettings);
    connect(m_spinSampleDuration, QOverload<int>::of(&QSpinBox::valueChanged), this, autoSaveHostSettings);
    connect(m_spinTolerance, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, autoSaveHostSettings);
    connect(m_txtRefDrxIp, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtRefDrxUser, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtRefDrxPass, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtDbHost, &QLineEdit::editingFinished, this, autoSaveHostSettings);
    connect(m_txtDbName, &QLineEdit::editingFinished, this, autoSaveHostSettings);

    onCheckDbClicked();
}

void HostWindow::setupLogsPage() {
    m_pageLogs = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout(m_pageLogs);
    vbox->setContentsMargins(8, 8, 8, 8);
    vbox->setSpacing(6);

    QTabWidget *logTabs = new QTabWidget(m_pageLogs);
    logTabs->setElideMode(Qt::ElideNone);
    logTabs->setUsesScrollButtons(false);
    logTabs->tabBar()->setExpanding(false);

    // Tab 0: Live Application & Mutex Logs
    QWidget *tabLive = new QWidget(logTabs);
    QVBoxLayout *liveBox = new QVBoxLayout(tabLive);
    liveBox->setContentsMargins(6, 6, 6, 6);

    QGroupBox *grpLogs = new QGroupBox("📜 Sunucu, Hakem (Mutex Arbiter) ve Referans Ethernet Logları", tabLive);
    QVBoxLayout *logBox = new QVBoxLayout(grpLogs);

    m_txtLog = new QTextEdit(grpLogs);
    m_txtLog->setReadOnly(true);
    m_txtLog->setStyleSheet("background-color: #040B09; color: #34D399; font-family: Consolas, 'Courier New', monospace; font-size: 9.5pt;");
    logBox->addWidget(m_txtLog);

    liveBox->addWidget(grpLogs);
    logTabs->addTab(tabLive, "📜 Canlı Sistem Logları");

    // Tab 1: System Event & Audit Logs (Audit)
    m_auditLogWidget = new TestHistoryWidget(TestHistoryWidget::ViewMode::AuditOnly, logTabs);
    connect(m_auditLogWidget, &TestHistoryWidget::logMessage, this, &HostWindow::appendLog);
    logTabs->addTab(m_auditLogWidget, "🛡️ Sistem Olay & Denetim Logları (Audit)");

    connect(logTabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == 1 && m_auditLogWidget) {
            m_auditLogWidget->refreshAuditLogs();
        }
    });

    vbox->addWidget(logTabs, 1);
}

void HostWindow::onRefEthPingSyncClicked() {
    if (m_txtHeaderRefIp) {
        const QString enteredIp = m_txtHeaderRefIp->text().trimmed();
        AppConfig &cfg = AppConfig::instance();
        if (!enteredIp.isEmpty() && enteredIp != cfg.refDrxEndpoint()) {
            cfg.setRefDrxHost(enteredIp);
            cfg.save();
            m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
            if (m_txtRefDrxIp) m_txtRefDrxIp->setText(cfg.refDrxHost());
            if (m_spinRefDrxPort) m_spinRefDrxPort->setValue(cfg.refDrxPort());
        }
    }
    m_lblRefPingBadge->setText("📶 Ref Ping: Aranıyor...");
    m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());
    m_refWebClient.testConnectionAndSync();
}

void HostWindow::onRefPingFinished(bool reachable, int latencyMs, const QString &hostIp) {
    if (reachable) {
        m_lblRefPingBadge->setText(QString("📶 Ref Ping: %1 ms (OK)").arg(latencyMs));
        m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());

        if (!m_refWebClient.isLoggedIn()) {
            m_lblRefTelemetry->setText("🟡 Ref Web: Oturum Açılıyor...");
            m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelAmberStyle());
        }
    } else {
        m_lblRefPingBadge->setText("❌ Ref Ping: Yanıt Yok");
        m_lblRefPingBadge->setStyleSheet(StyleHelper::getPastelRedStyle());

        m_lblRefTelemetry->setText(QString("🔴 Ref ETH: Yanıt Yok (%1)").arg(hostIp));
        m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelRedStyle());
    }
}

void HostWindow::onRefWebLoginSucceeded(int sessionKey) {
    m_lblRefTelemetry->setText(QString("🟢 Ref Web: Hazır (Key: %1)").arg(sessionKey));
    m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelGreenStyle());
    setStatusMessage(QString("Referans DRX Ethernet hazır (%1, SessionKey %2)").arg(m_refWebClient.host()).arg(sessionKey));
}

void HostWindow::onRefWebLoginFailed(const QString &reason) {
    m_lblRefTelemetry->setText("🔴 Ref Web Login Hata");
    m_lblRefTelemetry->setStyleSheet(StyleHelper::getPastelRedStyle());
    appendLog("[REF ETH HATA] Web CGI login başarısız: " + reason);
}

void HostWindow::onSaveRefWebSettingsClicked() {
    AppConfig &cfg = AppConfig::instance();
    cfg.setRefDrxHost(m_txtRefDrxIp->text().trimmed());
    if (m_spinRefDrxPort) cfg.setRefDrxPort(static_cast<quint16>(m_spinRefDrxPort->value()));
    cfg.setDrxUsername(m_txtRefDrxUser->text().trimmed());
    cfg.setDrxPassword(m_txtRefDrxPass->text());
    cfg.save();

    m_refWebClient.setHost(cfg.refDrxHost(), cfg.refDrxPort());
    m_refWebClient.setCredentials(cfg.drxUsername(), cfg.drxPassword());
    if (m_txtHeaderRefIp) {
        m_txtHeaderRefIp->setText(cfg.refDrxEndpoint());
    }

    onRefEthPingSyncClicked();
    DbManager::instance().logAudit(
        "SETTINGS_SAVED",
        QString("Host Referans DRX ağ ayarları güncellendi (%1:%2)")
            .arg(cfg.refDrxHost()).arg(cfg.refDrxPort()),
        cfg.currentUsername(), 0);
    QMessageBox::information(this, "Güncellendi", "Referans DRX Ethernet ayarları kaydedildi ve Ping testi başlatıldı.");
}

void HostWindow::onRefreshPortsClicked() {
    if (!m_comboSerialPorts) return;
    m_comboSerialPorts->clear();
    QStringList ports = SerialWorker::availablePorts();
    m_comboSerialPorts->addItems(ports);

    int idx = m_comboSerialPorts->findText(AppConfig::instance().serialPort());
    if (idx >= 0) m_comboSerialPorts->setCurrentIndex(idx);
}

void HostWindow::onConnectRefSerialClicked() {
    if (m_refSerial.isConnected()) {
        m_refSerial.closePort();
    } else if (m_comboSerialPorts) {
        QString port = m_comboSerialPorts->currentText();
        if (port.isEmpty()) {
            QMessageBox::warning(this, "Uyarı", "Lütfen bir seri port seçiniz!");
            return;
        }
        AppConfig::instance().setSerialPort(port);
        AppConfig::instance().save();
        m_refSerial.openPort(port, AppConfig::instance().serialBaudRate());
    }
}

void HostWindow::onStationConnected(int stationId, const QString &ip) {
    ensureStationCard(stationId);
    if (m_stationWidgets.contains(stationId)) {
        if (m_stationWidgets[stationId].lblConn) {
            m_stationWidgets[stationId].lblConn->setText("Bağlantı: 🟢 ÇEVRİMİÇİ");
            m_stationWidgets[stationId].lblConn->setStyleSheet("font-weight: bold; color: #34D399;");
        }
        m_stationWidgets[stationId].lblState->setText("DURUM: BAĞLANDI (BOŞTA)");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelBlueStyle());
        m_stationWidgets[stationId].lblIp->setText("IP Adresi: " + ip);
    }
    m_knownStations[stationId].stationId = stationId;
    m_knownStations[stationId].ipAddress = ip;
    m_knownStations[stationId].isConnected = true;
    m_knownStations[stationId].currentState = "BOŞTA";
    m_knownStations[stationId].lastHeartbeat = QDateTime::currentDateTime();
    DbManager::instance().upsertClusterNode(
        "STATION_CLIENT",
        QString("İstasyon %1").arg(stationId),
        ip,
        stationId,
        "ONLINE",
        QString(),
        QString("Station #%1").arg(stationId));
    updateLobbySummary();

    m_beaconBroadcaster.setStationCount(m_tcpServer.connectedStations().size());
    DbManager::instance().logAudit(
        "STATION_CONNECTED",
        QString("İstasyon %1 ağa bağlandı (IP: %2)").arg(stationId).arg(ip),
        "SYSTEM", stationId);
    appendLog(QString("İstasyon %1 ağa bağlandı (%2)").arg(stationId).arg(ip));
}

void HostWindow::onStationDisconnected(int stationId) {
    if (m_stationWidgets.contains(stationId)) {
        StationCardWidgets widgets = m_stationWidgets.take(stationId);
        if (widgets.cardFrame) {
            if (m_stationsGrid) {
                m_stationsGrid->removeWidget(widgets.cardFrame);
            }
            widgets.cardFrame->deleteLater();
        }
        // Re-pack remaining station cards cleanly so no gaps remain in the 3-column grid
        if (m_stationsGrid) {
            int idx = 0;
            for (auto it = m_stationWidgets.begin(); it != m_stationWidgets.end(); ++it) {
                if (it.value().cardFrame) {
                    m_stationsGrid->removeWidget(it.value().cardFrame);
                    m_stationsGrid->addWidget(it.value().cardFrame, idx / 3, idx % 3);
                    ++idx;
                }
            }
        }
    }
    if (m_stationWidgets.isEmpty() && m_lblNoStations) {
        m_lblNoStations->setVisible(true);
    }

    QString oldIp = m_knownStations.value(stationId).ipAddress;
    DbManager::instance().removeClusterNode("STATION_CLIENT", oldIp, stationId);
    m_knownStations.remove(stationId);
    updateLobbySummary();

    m_beaconBroadcaster.setStationCount(m_tcpServer.connectedStations().size());
    DbManager::instance().logAudit(
        "STATION_DISCONNECTED",
        QString("İstasyon %1 ağdan ayrıldı (IP: %2)").arg(stationId).arg(oldIp.isEmpty() ? "--" : oldIp),
        "SYSTEM", stationId);
    appendLog(QString("İstasyon %1 ağdan ayrıldı.").arg(stationId));
}

void HostWindow::onStationStatusUpdated(const StationLiveStatus &status) {
    int stId = status.stationId;
    ensureStationCard(stId, status.stationName);
    if (m_stationWidgets.contains(stId)) {
        auto &w = m_stationWidgets[stId];
        if (w.lblConn) {
            w.lblConn->setText(status.isConnected ? "Bağlantı: 🟢 ÇEVRİMİÇİ" : "Bağlantı: 🔴 BAĞLANTI YOK");
            w.lblConn->setStyleSheet(status.isConnected ? "font-weight: bold; color: #34D399;" : "font-weight: bold; color: #F87171;");
        }
        if (!status.ipAddress.isEmpty() && w.lblIp) {
            w.lblIp->setText("IP Adresi: " + status.ipAddress);
        }
        w.lblSn->setText(status.currentAntennaSn.isEmpty() ? "Anten Barkodu: --" : "Anten Barkodu: " + status.currentAntennaSn);
        w.lblChannel->setText(status.currentChannel > 0 ? QString("Aktif Kanal: %1").arg(status.currentChannel) : "Aktif Kanal: --");
        if (w.lblHeartbeat) {
            w.lblHeartbeat->setText("Son Sinyal: " + status.lastHeartbeat.toString("dd.MM hh:mm:ss"));
        }
    }
    m_knownStations[stId] = status;
    DbManager::instance().upsertClusterNode(
        "STATION_CLIENT",
        status.stationName.isEmpty() ? QString("İstasyon %1").arg(stId) : status.stationName,
        status.ipAddress,
        stId,
        status.currentAntennaSn.isEmpty() ? "ONLINE" : "BUSY",
        status.currentAntennaSn,
        status.currentState);
    updateLobbySummary();
}

void HostWindow::onLockGranted(int stationId, int timeoutSec) {
    m_lblArbiterStatus->setText(QString("🔒 KİLİTLİ: İstasyon %1 Test Yapıyor (Süre Sınırı: %2 sn)").arg(stationId).arg(timeoutSec));
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #FBBF24;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: TESTTE (REFERANS KİLİTLİ)");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelAmberStyle());
    }
    if (m_knownStations.contains(stationId)) {
        m_knownStations[stationId].currentState = "TESTTE (REFERANS KİLİTLİ)";
        updateLobbySummary();
    }
}

void HostWindow::onLockReleased(int stationId) {
    m_lblArbiterStatus->setText("🟢 Referans Alıcısı BOŞTA (Tüm İstasyonlar İçin Müsait)");
    m_lblArbiterStatus->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399;");

    if (m_stationWidgets.contains(stationId)) {
        m_stationWidgets[stationId].lblState->setText("DURUM: BOŞTA");
        m_stationWidgets[stationId].lblState->setStyleSheet(StyleHelper::getPastelGreenStyle());
    }
    if (m_knownStations.contains(stationId)) {
        m_knownStations[stationId].currentState = "BOŞTA";
        updateLobbySummary();
    }
}

void HostWindow::onLockExpired(int stationId) {
    appendLog(QString("UYARI: İstasyon %1 için kilit süresi aşıldı ve düşürüldü!").arg(stationId));
    onLockReleased(stationId);
}

void HostWindow::onRefreshHistoryClicked() {
    DbManager &db = DbManager::instance();
    if (!db.isConnected() && sender() != nullptr) {
        db.connectToDatabase(AppConfig::instance().dbHost(), AppConfig::instance().dbPort(),
                             AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                             AppConfig::instance().dbPassword());
    }

    if (m_historyWidget) {
        m_historyWidget->refreshHistory();
    }
    if (m_dbViewerWidget) {
        m_dbViewerWidget->refreshHistory();
    }
    loadWorkOrders();
}

void HostWindow::onSaveSettingsClicked() {
    AppConfig &cfg = AppConfig::instance();
    const quint16 oldTcpPort = cfg.tcpPort();

    cfg.setLockTimeoutSec(m_spinLockTimeout->value());
    cfg.setSampleDurationSec(m_spinSampleDuration->value());
    cfg.setDefaultToleranceDb(m_spinTolerance->value());
    if (m_spinHostTcpPort) cfg.setTcpPort(static_cast<quint16>(m_spinHostTcpPort->value()));
    if (m_spinHostUdpPort) cfg.setUdpBeaconPort(static_cast<quint16>(m_spinHostUdpPort->value()));
    if (m_txtDbHost) cfg.setDbHost(m_txtDbHost->text().trimmed());
    if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
    if (m_txtDbName) cfg.setDbName(m_txtDbName->text().trimmed());
    cfg.save();

    if (m_spinHostTcpPort && cfg.tcpPort() != oldTcpPort) {
        m_tcpServer.stop();
        m_tcpServer.start(cfg.tcpPort());
        m_lblServerStatus->setText(QString("🟢 TCP Sunucu Aktif (Port %1)").arg(cfg.tcpPort()));
    }

    DbManager::instance().logAudit(
        "SETTINGS_SAVED",
        QString("Host sistem ayarları güncellendi (Tolerans: ±%1 dB, Örnekleme: %2 sn, TCP: %3, DB: %4)")
            .arg(cfg.defaultToleranceDb(), 0, 'f', 2)
            .arg(cfg.sampleDurationSec())
            .arg(cfg.tcpPort())
            .arg(cfg.dbHost()),
        cfg.currentUsername(), 0);
    QMessageBox::information(this, "Kaydedildi", "Sistem parametreleri ve ağ/port ayarları başarıyla güncellendi.");
}

void HostWindow::onInstallMysqlClicked() {
    AppConfig &cfg = AppConfig::instance();
    if (m_txtDbHost) cfg.setDbHost(m_txtDbHost->text().trimmed());
    if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
    if (m_txtDbName) cfg.setDbName(m_txtDbName->text().trimmed());
    cfg.save();

    const bool ok = DbManager::instance().connectToDatabase(
        cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword());
    onCheckDbClicked();

    if (ok) {
        appendLog(QString("[Veritabanı] Merkezi MariaDB'ye bağlanıldı ve veriler yüklendi: %1:%2 (%3)")
                      .arg(cfg.dbHost()).arg(cfg.dbPort()).arg(cfg.dbName()));
        QMessageBox::information(this, "Veritabanı Bağlandı",
                                 QString("Merkezi MariaDB veritabanına başarıyla bağlanıldı ve veriler çekildi:\n%1:%2 (%3)")
                                     .arg(cfg.dbHost()).arg(cfg.dbPort()).arg(cfg.dbName()));
    } else {
        appendLog(QString("[Veritabanı HATA] Bağlanılamadı (%1:%2): %3")
                      .arg(cfg.dbHost()).arg(cfg.dbPort()).arg(DbManager::instance().lastError()));
        QMessageBox::warning(this, "Veritabanı Bağlantı Hatası",
                             QString("Merkezi MariaDB veritabanına bağlanılamadı (%1:%2):\n%3")
                                 .arg(cfg.dbHost()).arg(cfg.dbPort())
                                 .arg(DbManager::instance().lastError()));
    }
}

void HostWindow::onResetDbClicked() {
    AppConfig &cfg = AppConfig::instance();
    if (m_txtDbHost) cfg.setDbHost(m_txtDbHost->text().trimmed());
    if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
    if (m_txtDbName) cfg.setDbName(m_txtDbName->text().trimmed());
    cfg.save();

    if (!DbManager::instance().isConnected()) {
        DbManager::instance().connectToDatabase(
            cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword());
    }
    if (DbManager::instance().isConnected()) {
        DbManager::instance().ensureDefaultDataIfMissing();
        loadWorkOrders();
        const ReferenceMode activeMode = DbManager::instance().getReferenceMode();
        m_tcpServer.setReferenceMode(activeMode);
        updateRefModeBadgeUi(activeMode);
        if (m_goldenRefWidget) {
            m_goldenRefWidget->setActiveReferenceMode(activeMode);
            m_goldenRefWidget->reloadFrequencies();
        }
        if (m_historyWidget) m_historyWidget->refreshHistory();
        if (m_dbViewerWidget) m_dbViewerWidget->refreshHistory();
        if (m_userManager) m_userManager->reloadUsers();
        m_tcpServer.broadcastUserRoleUpdates();
        appendLog("[Veritabanı] Şema ve tüm veritabanı verileri yenilendi.");
    }
    onCheckDbClicked();
}

void HostWindow::onCheckDbClicked() {
    AppConfig &cfg = AppConfig::instance();
    if (m_txtDbHost && !m_txtDbHost->text().trimmed().isEmpty()) {
        cfg.setDbHost(m_txtDbHost->text().trimmed());
    }
    if (m_spinDbPort) cfg.setDbPort(m_spinDbPort->value());
    if (m_txtDbName && !m_txtDbName->text().trimmed().isEmpty()) {
        cfg.setDbName(m_txtDbName->text().trimmed());
    }

    if (sender() != nullptr && !DbManager::instance().isConnected()) {
        cfg.save();
        DbManager::instance().connectToDatabase(
            cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword());
    }

    const bool connected = DbManager::instance().isConnected();
    if (m_lblDbStatus) {
        if (connected) {
            m_lblDbStatus->setText(QString("✔ Bağlı (%1:%2 / %3)")
                                       .arg(cfg.dbHost()).arg(cfg.dbPort()).arg(cfg.dbName()));
            m_lblDbStatus->setStyleSheet("color: #34D399; font-weight: bold;");
        } else {
            m_lblDbStatus->setText("❌ Bağlı Değil / Servis Bekleniyor");
            m_lblDbStatus->setStyleSheet("color: #F87171; font-weight: bold;");
        }
    }
}

void HostWindow::onBackupDbNowClicked() {
    appendLog("💾 Manuel veritabanı yedekleme başlatılıyor...");
    QString outPath;
    QString outErr;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = BackupManager::instance().performBackup(&outPath, &outErr);
    QApplication::restoreOverrideCursor();

    if (ok) {
        QFileInfo fi(outPath);
        if (m_lblLastBackupInfo) {
            m_lblLastBackupInfo->setText(QString("Son Yedek: %1 (%2 KB) | Otomatik: Aktif (24s)")
                .arg(fi.fileName())
                .arg(fi.size() / 1024));
        }
        appendLog(QString("💾 Manuel yedekleme tamamlandı: %1 (%2 KB)").arg(fi.fileName()).arg(fi.size() / 1024));
        QMessageBox::information(this, "Yedekleme Başarılı",
            QString("Veritabanı yedeği başarıyla oluşturuldu:\n\nDosya: %1\nBoyut: %2 KB\nKonum: %3")
                .arg(fi.fileName())
                .arg(fi.size() / 1024)
                .arg(fi.absolutePath()));
    } else {
        appendLog(QString("❌ Yedekleme başarısız oldu: %1").arg(outErr));
        QMessageBox::warning(this, "Yedekleme Hatası",
            QString("Veritabanı yedeklenemedi:\n\n%1").arg(outErr));
    }
}

void HostWindow::onOpenBackupDirClicked() {
    QString dir = BackupManager::instance().backupDirectory();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void HostWindow::appendLog(const QString &msg) {
    AppLogger::instance().logEvent("HOST", msg);
    if (!m_txtLog) return;
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_txtLog->append(QString("[%1] %2").arg(timeStr, msg));
}

void HostWindow::setStatusMessage(const QString &msg) {
    if (m_statusLabel) {
        m_statusLabel->setText("✔ " + msg);
    }
}

void HostWindow::onAboutClicked() {
    AboutDialog dlg(this);
    dlg.exec();
}

void HostWindow::onLogoutClicked() {
    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        m_lblOperatorName->setText(AppConfig::instance().currentUserFullName());
        setStatusMessage("Yönetici oturumu yenilendi: " + AppConfig::instance().currentUserFullName());
    }
}

void HostWindow::onReturnLobbyClicked() {
    m_returningToLobby = true;
    shutdownHostServices();
    LobbyWindow *lobbyWin = new LobbyWindow();
    lobbyWin->show();
    this->close();
}

void HostWindow::updateRefModeBadgeUi(ReferenceMode mode) {
    if (!m_lblRefModeBadge) return;
    if (mode == ReferenceMode::GoldenSample) {
        m_lblRefModeBadge->setText("🌟 Mod: Golden Sample (Kayıtlı)");
        m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelAmberStyle());
    } else {
        m_lblRefModeBadge->setText("🟢 Mod: Canlı Referans");
        m_lblRefModeBadge->setStyleSheet(StyleHelper::getPastelGreenStyle());
    }
}

void HostWindow::onReferenceModeApplyRequested(ReferenceMode mode) {
    AppConfig::instance().setLastReferenceMode(mode);
    DbManager::instance().setReferenceMode(mode);
    m_tcpServer.setReferenceMode(mode);
    updateRefModeBadgeUi(mode);

    QString modeDesc = (mode == ReferenceMode::GoldenSample)
                           ? "Kayıtlı Golden Sample (Referans Anten Olmadan)"
                           : "Canlı Referans Anten (Live Reference)";
    setStatusMessage("Aktif referans modu güncellendi: " + modeDesc);
    QMessageBox::information(this, "Referans Modu Güncellendi",
                             QString("Tüm test istasyonları için aktif referans modu güncellendi:\n\n• %1").arg(modeDesc));
}
