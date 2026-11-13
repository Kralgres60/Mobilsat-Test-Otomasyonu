#include "dependency_install_dialog.h"
#include "core/dependency_installer.h"
#include "core/app_config.h"
#include "database/db_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QClipboard>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

DependencyInstallDialog::DependencyInstallDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("NETA DRX9000 - Veritabanı ve Bağımlılık Kurulum Sihirbazı");
    setFixedSize(780, 520);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setStyleSheet("background-color: #11141A; color: #E0E6ED; font-family: 'Segoe UI', Arial, sans-serif;");

    QString script = DependencyInstaller::findPsInstallerScript();
    if (!script.isEmpty()) {
        m_streamLogPath = QFileInfo(script).dir().filePath("install_stream.log");
    }

    setupUi();

    connect(&m_pollTimer, &QTimer::timeout, this, &DependencyInstallDialog::onPollTimer);

    // Start installation as soon as dialog opens
    QTimer::singleShot(200, this, &DependencyInstallDialog::startInstallation);
}

DependencyInstallDialog::~DependencyInstallDialog() {
    m_pollTimer.stop();
}

void DependencyInstallDialog::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 1. Top Title and Status
    QVBoxLayout *headerLayout = new QVBoxLayout();
    headerLayout->setSpacing(4);

    m_lblTitle = new QLabel("⚡ MariaDB / MySQL Servisi Kuruluyor...", this);
    m_lblTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #64B5F6;");

    m_lblSubtitle = new QLabel("Lütfen açılan Windows Yönetici (UAC) onay penceresinde 'EVET' seçiniz. Kurulum adımları canlı olarak aşağıda listelenecektir:", this);
    m_lblSubtitle->setStyleSheet("font-size: 12px; color: #9CA3AF;");
    m_lblSubtitle->setWordWrap(true);

    headerLayout->addWidget(m_lblTitle);
    headerLayout->addWidget(m_lblSubtitle);
    mainLayout->addLayout(headerLayout);

    // 2. Progress Bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setTextVisible(false);
    m_progressBar->setRange(0, 0); // Çalışırken sonsuz animasyon
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #1F2937; border-radius: 4px; border: none; }"
        "QProgressBar::chunk { background-color: #2196F3; border-radius: 4px; }"
    );
    mainLayout->addWidget(m_progressBar);

    // 3. Live Debug Console (QPlainTextEdit)
    m_txtConsole = new QPlainTextEdit(this);
    m_txtConsole->setReadOnly(true);
    m_txtConsole->setStyleSheet(
        "QPlainTextEdit {"
        "  background-color: #0A0D14;"
        "  color: #81A1C1;"
        "  border: 1px solid #232936;"
        "  border-radius: 6px;"
        "  font-family: 'Consolas', 'Courier New', monospace;"
        "  font-size: 11px;"
        "  padding: 8px;"
        "}"
    );
    mainLayout->addWidget(m_txtConsole);

    // 4. Footer Badge and Control Buttons
    QHBoxLayout *botLayout = new QHBoxLayout();
    m_lblPortStatus = new QLabel("Port 3306 (MySQL): Bekleniyor...", this);
    m_lblPortStatus->setStyleSheet("font-size: 12px; font-weight: bold; color: #FFA726;");
    botLayout->addWidget(m_lblPortStatus);

    botLayout->addStretch();

    m_btnCopy = new QPushButton("📋 Logları Kopyala", this);
    m_btnCopy->setFixedHeight(36);
    m_btnCopy->setStyleSheet("background-color: #1F2937; color: #D1D5DB; font-weight: bold; padding: 0 16px; border-radius: 6px;");
    connect(m_btnCopy, &QPushButton::clicked, this, &DependencyInstallDialog::onCopyLogClicked);
    botLayout->addWidget(m_btnCopy);

    m_btnRetry = new QPushButton("🔄 Yeniden Dene", this);
    m_btnRetry->setFixedHeight(36);
    m_btnRetry->setStyleSheet("background-color: #E65100; color: #FFFFFF; font-weight: bold; padding: 0 16px; border-radius: 6px;");
    m_btnRetry->setVisible(false);
    connect(m_btnRetry, &QPushButton::clicked, this, &DependencyInstallDialog::onRetryClicked);
    botLayout->addWidget(m_btnRetry);

    m_btnClose = new QPushButton("İptal", this);
    m_btnClose->setFixedHeight(36);
    m_btnClose->setMinimumWidth(120);
    m_btnClose->setStyleSheet("background-color: #374151; color: #F3F4F6; font-weight: bold; padding: 0 16px; border-radius: 6px;");
    connect(m_btnClose, &QPushButton::clicked, this, &DependencyInstallDialog::onCloseOrFinishClicked);
    botLayout->addWidget(m_btnClose);

    mainLayout->addLayout(botLayout);
}

void DependencyInstallDialog::startInstallation() {
    m_txtConsole->clear();
    m_logPos = 0;
    m_elapsedTicks = 0;
    m_finished = false;
    m_success = false;

    m_progressBar->setRange(0, 0);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #1F2937; border-radius: 4px; border: none; }"
        "QProgressBar::chunk { background-color: #2196F3; border-radius: 4px; }"
    );
    m_lblTitle->setText("⚡ MariaDB / MySQL Servisi Kuruluyor...");
    m_lblTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #64B5F6;");
    m_lblSubtitle->setText("Lütfen açılan Windows Yönetici (UAC) onay penceresinde 'EVET' seçiniz. Kurulum adımları canlı olarak aşağıda listelenecektir:");
    m_btnClose->setText("İptal");
    m_btnClose->setStyleSheet("background-color: #374151; color: #F3F4F6; font-weight: bold; padding: 0 16px; border-radius: 6px;");
    m_btnRetry->setVisible(false);

    appendLog("[BİLGİ] NETA DRX9000 Veritabanı Kurulum Motoru başlatılıyor...");

    // Remove old log file
    if (!m_streamLogPath.isEmpty() && QFile::exists(m_streamLogPath)) {
        QFile::remove(m_streamLogPath);
    }

    QString script = DependencyInstaller::findPsInstallerScript();
    if (script.isEmpty()) {
        appendLog("[BİLGİ] Dahili Portable MariaDB motoru başlatılıyor...");
        m_lblSubtitle->setText("Dahili Portable MariaDB veritabanı motoru başlatılıyor, lütfen bekleyiniz...");
        if (DbManager::instance().startPortableMariaDbIfPresent()) {
            m_pollTimer.start(150);
            return;
        }
        appendLog("[HATA] Ne install_mysql_auto.ps1 ne de tools/mariadb_portable bulunamadı!");
        finishInstallation(false, "Taşınabilir MariaDB motoru başlatılamadı.");
        return;
    }

    QString nativePsScript = QDir::toNativeSeparators(script);
    QString nativeLogFile = QDir::toNativeSeparators(m_streamLogPath);
    QDir workDir = QFileInfo(script).dir();

    appendLog(QString("[BİLGİ] PowerShell kurulum motoru bulundu: %1").arg(nativePsScript));
    appendLog("[BİLGİ] Windows Yönetici (UAC) onayı isteniyor...");

#ifdef Q_OS_WIN
    // Launch PowerShell with admin privileges (runas) to write output to screen and log file
    QString params = QString("-NoProfile -ExecutionPolicy Bypass -Command \"& '%1' *>&1 | Tee-Object -FilePath '%2'\"")
                         .arg(nativePsScript, nativeLogFile);

    HINSTANCE hInst = ShellExecuteW(
        nullptr,
        L"runas",
        L"powershell.exe",
        reinterpret_cast<LPCWSTR>(params.utf16()),
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(workDir.absolutePath()).utf16()),
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(hInst) <= 32) {
        appendLog("[UYARI] Windows Yönetici (UAC) onayı verilmedi veya iptal edildi.");
        finishInstallation(false, "Yönetici yetkisi verilmedi.");
        return;
    }
#endif

    appendLog("[BİLGİ] Kurulum işlemi arka planda başlatıldı. Konsol çıktıları dinleniyor...");
    m_pollTimer.start(120); // 120 ms aralıkla logları oku ve portu yokla
}

void DependencyInstallDialog::onPollTimer() {
    m_elapsedTicks++;

    // 1. Read new lines from log file
    readStreamLog();

    // 2. Check Port 3306 every second
    if (m_elapsedTicks % 8 == 0) {
        bool listening = DependencyInstaller::isMySqlListening("127.0.0.1", 3306);
        if (listening) {
            m_lblPortStatus->setText("Port 3306 (MySQL): ✔ AÇIK & DİNLENİYOR");
            m_lblPortStatus->setStyleSheet("font-size: 12px; font-weight: bold; color: #4CAF50;");

            appendLog("\n[BAŞARILI] MySQL Port 3306 aktifleşti!");
            appendLog("[BİLGİ] Fabrika test tabloları ve veritabanı bağlantısı doğrulanıyor...");

            AppConfig &cfg = AppConfig::instance();
            bool connected = DbManager::instance().connectToDatabase(
                cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPassword()
            );

            if (connected) {
                appendLog("[BAŞARILI] Veritabanına başarıyla bağlanıldı.");
                DbManager::instance().ensureDefaultDataIfMissing();
                appendLog("[BAŞARILI] Fabrika şeması (schema_mysql.sql) ve 10 Türksat frekansı (seed_data.sql) hazır.");
                finishInstallation(true, "MariaDB / MySQL servisi ve fabrika tabloları başarıyla kuruldu!");
                return;
            } else {
                appendLog(QString("[UYARI] Port açık fakat bağlantı henüz kurulamadı: %1. Bekleniyor...").arg(DbManager::instance().lastError()));
            }
        } else {
            int sec = m_elapsedTicks * 120 / 1000;
            m_lblPortStatus->setText(QString("Port 3306: Bekleniyor... (%1 sn)").arg(sec));
        }

        // Timeout check (120 seconds)
        if (m_elapsedTicks > (120 * 1000 / 120)) {
            finishInstallation(false, "Zaman aşımı: MySQL servisi 120 saniye içinde yanıt vermedi.");
        }
    }
}

void DependencyInstallDialog::readStreamLog() {
    if (m_streamLogPath.isEmpty() || !QFile::exists(m_streamLogPath)) {
        return;
    }

    QFile file(m_streamLogPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    if (file.size() > m_logPos) {
        file.seek(m_logPos);
        QByteArray newBytes = file.readAll();
        m_logPos = file.pos();

        QString text = QString::fromUtf8(newBytes);
        if (text.isEmpty()) {
            text = QString::fromLocal8Bit(newBytes);
        }

        QStringList lines = text.split('\n');
        for (const QString &line : lines) {
            QString cleanLine = line.trimmed();
            if (!cleanLine.isEmpty()) {
                appendLog(cleanLine);
            }
        }
    }
    file.close();
}

void DependencyInstallDialog::appendLog(const QString &line) {
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_txtConsole->appendPlainText(QString("[%1] %2").arg(timeStr, line));
    m_txtConsole->ensureCursorVisible();
}

void DependencyInstallDialog::finishInstallation(bool success, const QString &message) {
    m_pollTimer.stop();
    m_finished = true;
    m_success = success;

    // Read remaining lines
    readStreamLog();

    m_progressBar->setRange(0, 100);

    if (success) {
        m_progressBar->setValue(100);
        m_progressBar->setStyleSheet(
            "QProgressBar { background-color: #1F2937; border-radius: 4px; border: none; }"
            "QProgressBar::chunk { background-color: #2E7D32; border-radius: 4px; }"
        );
        m_lblTitle->setText("✔ Kurulum Başarıyla Tamamlandı!");
        m_lblTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #4CAF50;");
        m_lblSubtitle->setText(message.isEmpty() ? "Tüm veritabanı servisleri ve fabrika test şeması hazır." : message);

        m_btnClose->setText("Tamamla ve Sistemi Başlat ➔");
        m_btnClose->setStyleSheet("background-color: #2E7D32; color: #FFFFFF; font-size: 13px; font-weight: bold; padding: 0 20px; border-radius: 6px;");
        m_btnRetry->setVisible(false);
    } else {
        m_progressBar->setValue(0);
        m_progressBar->setStyleSheet(
            "QProgressBar { background-color: #1F2937; border-radius: 4px; border: none; }"
            "QProgressBar::chunk { background-color: #C62828; border-radius: 4px; }"
        );
        m_lblTitle->setText("❌ Kurulum Tamamlanamadı");
        m_lblTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #EF5350;");
        m_lblSubtitle->setText(message.isEmpty() ? "Lütfen yukarıdaki hata loglarını inceleyiniz ve Yönetici olarak tekrar deneyiniz." : message);

        m_btnClose->setText("Kapat");
        m_btnClose->setStyleSheet("background-color: #374151; color: #F3F4F6; font-weight: bold; padding: 0 16px; border-radius: 6px;");
        m_btnRetry->setVisible(true);
    }
}

void DependencyInstallDialog::onRetryClicked() {
    startInstallation();
}

void DependencyInstallDialog::onCopyLogClicked() {
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(m_txtConsole->toPlainText());
    QMessageBox::information(this, "Kopyalandı", "Tüm kurulum debug logları panoya kopyalandı.");
}

void DependencyInstallDialog::onCloseOrFinishClicked() {
    if (m_success) {
        accept();
    } else {
        reject();
    }
}
