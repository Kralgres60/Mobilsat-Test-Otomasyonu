/**
 * @file login_dialog.cpp
 * @brief Implementation of the minimalist NETA Green authentication dialog (no station/host selection here).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "login_dialog.h"
#include "database/db_manager.h"
#include "core/appinfo.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QIcon>
#include <QPixmap>
#include <QShowEvent>
#include <QGuiApplication>
#include <QScreen>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(QString("%1 - Kullanıcı Girişi").arg(NETA_APP_NAME));
    setFixedSize(430, 420);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowIcon(QIcon(":/resources/images/neta_app.ico"));

    setupUi();

    m_txtUsername->setText(AppConfig::instance().currentUsername());
    m_txtPassword->setFocus();
}

void LoginDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QRect screenGeom = screen->availableGeometry();
        int x = screenGeom.x() + (screenGeom.width() - width()) / 2;
        int y = screenGeom.y() + (screenGeom.height() - height()) / 2;
        move(x, y);
    }
}

void LoginDialog::setupUi() {
    setStyleSheet(R"(
        QDialog {
            background-color: #071210;
        }
        QGroupBox {
            font-weight: bold;
            font-size: 10pt;
            color: #34D399;
            border: 1px solid #1A3D2B;
            border-radius: 8px;
            margin-top: 10px;
            padding-top: 14px;
            background-color: #0A1F17;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
            color: #34D399;
        }
        QLabel {
            color: #E6F4EE;
            font-size: 9.5pt;
            font-weight: bold;
        }
        QLineEdit {
            background-color: #112D21;
            color: #E6F4EE;
            border: 1px solid #1A3D2B;
            border-radius: 6px;
            padding: 8px 12px;
            font-size: 10pt;
            font-weight: bold;
            min-height: 22px;
        }
        QLineEdit:focus {
            border: 1px solid #10B981;
        }
        QPushButton#btnLogin {
            background-color: #10B981;
            color: #071210;
            border: 1px solid #34D399;
            font-weight: bold;
            font-size: 10.5pt;
            border-radius: 6px;
            padding: 10px 20px;
            min-height: 40px;
        }
        QPushButton#btnLogin:hover {
            background-color: #34D399;
            color: #071210;
            border-color: #6EE7B7;
        }
        QPushButton#btnLogin:pressed {
            background-color: #059669;
            color: #FFFFFF;
        }
    )");

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(12);

    QHBoxLayout *logoLayout = new QHBoxLayout();
    QLabel *lblLogo = new QLabel(this);
    lblLogo->setMaximumSize(160, 60);
    lblLogo->setAlignment(Qt::AlignCenter);
    lblLogo->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 4px 12px;");
    QPixmap pix(":/resources/images/logo.png");
    if (!pix.isNull()) {
        lblLogo->setPixmap(pix.scaled(140, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        lblLogo->setText("NETA");
        lblLogo->setStyleSheet("font-size: 24pt; font-weight: bold; color: #064E3B; background-color: #FFFFFF; border-radius: 6px; padding: 4px 14px;");
    }
    logoLayout->addStretch();
    logoLayout->addWidget(lblLogo);
    logoLayout->addStretch();
    rootLayout->addLayout(logoLayout);

    QLabel *lblTitleHeader = new QLabel("DRX ANTEN TESTİ KULLANICI GİRİŞİ", this);
    lblTitleHeader->setAlignment(Qt::AlignCenter);
    lblTitleHeader->setStyleSheet("font-size: 13pt; font-weight: bold; color: #34D399; padding: 2px 0;");
    rootLayout->addWidget(lblTitleHeader);

    QGroupBox *groupLoginBox = new QGroupBox("Kullanıcı Kimlik Doğrulama", this);
    QFormLayout *formLayout = new QFormLayout(groupLoginBox);
    formLayout->setContentsMargins(16, 18, 16, 16);
    formLayout->setHorizontalSpacing(12);
    formLayout->setVerticalSpacing(12);
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QLabel *lblUsername = new QLabel("Kullanıcı Adı:", groupLoginBox);
    m_txtUsername = new QLineEdit(groupLoginBox);
    m_txtUsername->setPlaceholderText("Kullanıcı adınızı girin...");

    QLabel *lblPassword = new QLabel("Şifre:", groupLoginBox);
    m_txtPassword = new QLineEdit(groupLoginBox);
    m_txtPassword->setEchoMode(QLineEdit::Password);
    m_txtPassword->setPlaceholderText("Şifrenizi girin...");

    formLayout->addRow(lblUsername, m_txtUsername);
    formLayout->addRow(lblPassword, m_txtPassword);
    rootLayout->addWidget(groupLoginBox);

    m_lblErrorStatus = new QLabel("", this);
    m_lblErrorStatus->setAlignment(Qt::AlignCenter);
    m_lblErrorStatus->setStyleSheet("color: #EF4444; font-weight: bold; font-size: 9.5pt;");
    m_lblErrorStatus->setMinimumHeight(20);
    rootLayout->addWidget(m_lblErrorStatus);

    m_btnLogin = new QPushButton("GİRİŞ YAP", this);
    m_btnLogin->setObjectName("btnLogin");
    m_btnLogin->setCursor(Qt::PointingHandCursor);
    rootLayout->addWidget(m_btnLogin);

    connect(m_btnLogin, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(m_txtUsername, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(m_txtPassword, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
}

void LoginDialog::onLoginClicked() {
    QString user = m_txtUsername->text().trimmed();
    QString pass = m_txtPassword->text();

    if (user.isEmpty()) {
        m_lblErrorStatus->setText("Lütfen kullanıcı adını girin!");
        m_txtUsername->setFocus();
        return;
    }
    if (pass.trimmed().isEmpty()) {
        m_lblErrorStatus->setText("Lütfen şifrenizi girin!");
        m_txtPassword->setFocus();
        return;
    }

    UserRole role = UserRole::Technician;
    QString fullName;
    if (!DbManager::instance().authenticateUser(user, pass, role, fullName)) {
        m_lblErrorStatus->setText("Hatalı kullanıcı adı veya şifre!");
        m_txtPassword->selectAll();
        m_txtPassword->setFocus();
        return;
    }

    m_authenticatedRole = role;
    m_authenticatedUsername = user;
    m_authenticatedFullName = fullName;

    AppConfig &cfg = AppConfig::instance();
    cfg.setUserRole(m_authenticatedRole);
    cfg.setCurrentUsername(m_authenticatedUsername);
    cfg.setCurrentUserFullName(m_authenticatedFullName);
    cfg.save();

    accept();
}
